#include "util.h"
#include <netinet/tcp.h>
#include <nghttp2/nghttp2.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

static void
register_signal_handlers()
{
    signal(SIGPIPE, SIG_IGN);
}

static void
openssl_init()
{
    SSL_load_error_strings();
    SSL_library_init();
}

static void
set_sock_option(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        log_fatal("fcntl error, %s", strerror(errno));
    }
    int rc = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    if (rc < 0) {
        log_fatal("fcntl error, %s", strerror(errno));
    }

    int val = 1;
    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &val, (socklen_t)sizeof(val));
    if (rc < 0) {
        log_fatal("setsockopt error, %s", strerror(errno));
    }
}

static int
tcp_connect(const char *host, uint16_t port)
{
    log_info("start to resolve %s", host);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;

    char portstr[10];
    snprintf(portstr, sizeof(portstr), "%u", port);

    int ec = getaddrinfo(host, portstr, &hints, &res);
    if (ec) {
        log_fatal("getaddrinfo error: %s", gai_strerror(ec));
    }

    char ip[128];

    if (res->ai_family == AF_INET) {
        inet_ntop(
            res->ai_family, &((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr, ip, sizeof(ip));
    } else if (res->ai_family == AF_INET6) {
        inet_ntop(res->ai_family, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr.s6_addr, ip,
            sizeof(ip));
    } else {
        snprintf(ip, sizeof(ip), "-");
    }

    log_info("resolve %s done, choose ip %s", host, ip);

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (sockfd < 0) {
        log_fatal("socket create failed: %s", strerror(errno));
    }

    log_info("connect to %s:%u ...", ip, port);

    int rc = connect(sockfd, res->ai_addr, res->ai_addrlen);
    if (rc < 0) {
        log_fatal("connect failed: %s", strerror(errno));
    }

    log_info("connect done!");
    freeaddrinfo(res);

    return sockfd;
}

static int is_https = 0;
static SSL *ssl = NULL;
static int want_read = 0;
static int want_write = 0;
static int sockfd = -1;
static nghttp2_session *sess = NULL;

static ssize_t
on_send(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *userp)
{
    int rc = 0;
    want_write = 0;
    ERR_clear_error();

    log_debug("rua!");

    if (ssl) {
        rc = SSL_write(ssl, data, (int)length);
        if (rc <= 0) {
            int err = SSL_get_error(ssl, rc);
            int is_fail = 0;
            if (err == SSL_ERROR_WANT_WRITE) {
                want_write = 1;
            } else if (err == SSL_ERROR_WANT_READ) {
                want_read = 1;
            } else {
                is_fail = 1;
            }
            rc = is_fail ? NGHTTP2_ERR_CALLBACK_FAILURE : NGHTTP2_ERR_WOULDBLOCK;
        }
    } else {
        rc = write(sockfd, data, (int)length);
        if (rc <= 0) {
            if (errno == EAGAIN || errno == EINTR) {
                want_write = 1;
                rc = NGHTTP2_ERR_WOULDBLOCK;
            } else {
                rc = NGHTTP2_ERR_CALLBACK_FAILURE;
            }
        }
    }

    return rc;
}

static ssize_t
on_recv(nghttp2_session *session, uint8_t *buf, size_t length, int flags, void *userp)
{
    int rc = 0;
    want_read = 0;
    ERR_clear_error();

    log_debug("rua!");

    if (ssl) {
        rc = SSL_read(ssl, buf, (int)length);
        if (rc < 0) {
            int err = SSL_get_error(ssl, rc);
            int is_fail = 0;
            if (err == SSL_ERROR_WANT_WRITE) {
                want_write = 1;
            } else if (err == SSL_ERROR_WANT_READ) {
                want_read = 1;
            } else {
                is_fail = 1;
            }
            rc = is_fail ? NGHTTP2_ERR_CALLBACK_FAILURE : NGHTTP2_ERR_WOULDBLOCK;
        } else if (rc == 0) {
            rc = NGHTTP2_ERR_EOF;
        }
    } else {
        rc = read(sockfd, buf, (int)length);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                want_write = 1;
                rc = NGHTTP2_ERR_WOULDBLOCK;
            } else {
                rc = NGHTTP2_ERR_CALLBACK_FAILURE;
            }
        } else if (rc == 0) {
            rc = NGHTTP2_ERR_EOF;
        }
    }

    return rc;
}

static int
on_stream_close(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *userp)
{
    log_info("stream id %d is closing, error_code is %u", stream_id, error_code);

    int rc = nghttp2_session_terminate_session(session, NGHTTP2_NO_ERROR);
    if (rc) {
        log_fatal("nghttp2_session_terminate_session return %d", rc);
    }

    return 0;
}

static const char *
type_to_str(uint8_t type)
{
    static char unknow_buf[128];

    switch (type) {
    case NGHTTP2_DATA:
        return "DATA";
    case NGHTTP2_HEADERS:
        return "HEADERS";
    case NGHTTP2_PRIORITY:
        return "PRIORITY";
    case NGHTTP2_RST_STREAM:
        return "RST_STREAM";
    case NGHTTP2_SETTINGS:
        return "SETTINGS";
    case NGHTTP2_PUSH_PROMISE:
        return "PUSH_PROMISE";
    case NGHTTP2_PING:
        return "PING";
    case NGHTTP2_GOAWAY:
        return "GOAWAY";
    case NGHTTP2_WINDOW_UPDATE:
        return "WINDOW_UPDATE";
    case NGHTTP2_CONTINUATION:
        return "CONTINUATION";
    case NGHTTP2_ALTSVC:
        return "ALTSVC";
    case NGHTTP2_ORIGIN:
        return "ORIGIN";
    }

    snprintf(unknow_buf, sizeof(unknow_buf), "Unknow type %u", type);
    return unknow_buf;
}

static const char *
flags_to_str(uint8_t flags)
{
    static char flags_buf[128];
    char *p = flags_buf;
    size_t len = sizeof(flags_buf);

    int n = snprintf(p, len, "{");
    p += n;
    len -= n;

    if (flags & NGHTTP2_FLAG_END_STREAM) {
        n = snprintf(p, len, "end_stream;");
        p += n;
        len -= n;
    }

    if (flags & NGHTTP2_FLAG_END_HEADERS) {
        n = snprintf(p, len, "end_headers;");
        p += n;
        len -= n;
    }

    if (flags & NGHTTP2_FLAG_ACK) {
        n = snprintf(p, len, "ack;");
        p += n;
        len -= n;
    }

    if (flags & NGHTTP2_FLAG_PADDED) {
        n = snprintf(p, len, "padded;");
        p += n;
        len -= n;
    }

    if (flags & NGHTTP2_FLAG_PRIORITY) {
        n = snprintf(p, len, "priority;");
        p += n;
        len -= n;
    }

    if (flags == NGHTTP2_FLAG_NONE) {
        n = snprintf(p, len, "none;");
        p += n;
        len -= n;
    }

    snprintf(p, len, "}");

    return flags_buf;
}

static void
process_headers_frame(const nghttp2_headers *headers)
{
    const nghttp2_nv *nva = headers->nva;

    for (int i = 0; i < headers->nvlen; i++) {
        log_info("%.*s: %.*s", nva[i].namelen, nva[i].name, nva[i].valuelen, nva[i].value);
    }
}

static void
process_rst_stream_frame(const nghttp2_rst_stream *rst_stream)
{
    log_info("rst error code is %u", rst_stream->error_code);
}

static void
print_origin_extension(const nghttp2_ext_origin *origin)
{
    for (size_t i = 0; i < origin->nov; i++) {
        nghttp2_origin_entry *ov = &origin->ov[i];
        log_info("origin host <%.*s>", (int)ov->origin_len, ov->origin);
    }
}

static void
process_http2_frame(const nghttp2_frame *frame, int c_to_s)
{
    const char *base
        = c_to_s ? "C ----------------------------> S" : "C <---------------------------- S";

    uint8_t type = frame->hd.type;
    int32_t stream_id = frame->hd.stream_id;
    const char *type_str = type_to_str(type);
    size_t length = frame->hd.length;
    const char *flags_str = flags_to_str(frame->hd.flags);

    log_info(
        "%s (%s) stream_id:%d length:%lu flags:%s", base, type_str, stream_id, length, flags_str);

    switch (type) {
    case NGHTTP2_HEADERS:
        process_headers_frame(&frame->headers);
        break;

    case NGHTTP2_RST_STREAM:
        process_rst_stream_frame(&frame->rst_stream);
        break;

    case NGHTTP2_ORIGIN:
        {
            nghttp2_ext_origin *origin = (nghttp2_ext_origin *)frame->ext.payload;
            if (origin) {
                print_origin_extension(origin);
            }
        }

        break;
    }
}

static int
on_send_frame(nghttp2_session *session, const nghttp2_frame *frame, void *userp)
{
    process_http2_frame(frame, 1);
    return 0;
}

static int
on_recv_frame(nghttp2_session *session, const nghttp2_frame *frame, void *userp)
{
    process_http2_frame(frame, 0);
    return 0;
}

static int
on_recv_header(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name,
    size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *userp)
{
    int32_t stream_id = frame->hd.stream_id;

    log_info("[%d] %.*s: %.*s", stream_id, namelen, name, valuelen, value);

    return 0;
}

static int
on_recv_data_chunk(nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data,
    size_t len, void *userp)
{
    log_info("(DATA chunk %lu bytes)\n%.*s", len, len, data);
    return 0;
}

static void
nghttp2_session_callback_setup(nghttp2_session_callbacks *callbacks)
{
    nghttp2_session_callbacks_set_send_callback(callbacks, on_send);
    nghttp2_session_callbacks_set_recv_callback(callbacks, on_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close);
    nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, on_send_frame);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_recv_frame);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_recv_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_recv_data_chunk);
}

static void
submit_settings()
{
    nghttp2_settings_entry settings[] = { { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100 },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535 } };

    int rc = nghttp2_submit_settings(sess, NGHTTP2_FLAG_NONE, settings, array_size(settings));
    if (rc) {
        log_fatal("nghttp2_submit_settings return %d", rc);
    }
}

static void
submit_request(URI *uri)
{
#define MAKE_NV(NAME, VALUE)                                                                    \
    {                                                                                              \
        (uint8_t *)NAME, (uint8_t *)VALUE, sizeof(NAME) - 1, strlen(VALUE), NGHTTP2_NV_FLAG_NONE   \
    }

    char hostport[1024];
    snprintf(hostport, sizeof(hostport), "%s", uri->host);

    const nghttp2_nv nva[] = { MAKE_NV(":method", "GET"), MAKE_NV(":path", uri->path),
        MAKE_NV(":scheme", uri->schema), MAKE_NV(":authority", hostport),
        MAKE_NV("accept", "*/*"), MAKE_NV("user-agent", "h2client/v0.0.1") };

    int32_t stream_id = nghttp2_submit_request(sess, NULL, nva, array_size(nva), NULL, NULL);

    if (stream_id < 0) {
        log_fatal("nghttp2_submit_request return %d", stream_id);
    }

    log_info("open stream, id is %d", stream_id);
}

static void
run_event_loop()
{
    while (1) {
        int need_read = nghttp2_session_want_read(sess);
        int need_write = nghttp2_session_want_write(sess);

        if (!need_read && !need_write) {
            break;
        }

        if (want_read) {
            need_read = 1;
        }
        if (want_write) {
            need_write = 1;
        }

        struct pollfd fds[1];
        fds[0].fd = sockfd;
        fds[0].events = 0;
        if (need_read) {
            fds[0].events |= POLLIN;
        }
        if (need_write) {
            fds[0].events |= POLLOUT;
        }

        int rc = poll(fds, array_size(fds), -1);
        if (rc < 0) {
            log_fatal("poll return %d, %s", rc, strerror(errno));
        }

        int revents = fds[0].revents;
        if (revents & POLLIN) {
            rc = nghttp2_session_recv(sess);
            if (rc) {
                log_fatal("nghttp2_session_recv return %d", rc);
            }
        }
        if (revents & POLLOUT) {
            rc = nghttp2_session_send(sess);
            if (rc) {
                log_fatal("nghttp2_session_send return %d", rc);
            }
        }
    }
}

static int
select_next_proto_cb(SSL *ssl, unsigned char **out, unsigned char *outlen, const unsigned char *in,
    unsigned int inlen, void *arg)
{
    int rc = nghttp2_select_next_protocol(out, outlen, in, inlen);
    if (rc <= 0) {
        log_fatal("server dont support HTTP/2 protocol");
    }
    return SSL_TLSEXT_ERR_OK;
}

static void
ssl_handshake()
{
    int rc = SSL_set_fd(ssl, sockfd);
    if (rc == 0) {
        log_fatal("SSL_set_fd error, %s", ERR_error_string(ERR_get_error(), NULL));
    }
    ERR_clear_error();
    rc = SSL_connect(ssl);
    if (rc <= 0) {
        log_fatal("SSL_connect error, %s", ERR_error_string(ERR_get_error(), NULL));
    }
}

static void
fetch_uri(URI *uri)
{
    log_info("start to fetch schema <%s> host <%s> port <%u> path <%s>", uri->schema, uri->host,
        uri->port, uri->path);

    sockfd = tcp_connect(uri->host, uri->port);

    SSL_CTX *ssl_ctx = NULL;
    is_https = !strcmp(uri->schema, "https");
    if (is_https) {
        ssl_ctx = SSL_CTX_new(SSLv23_client_method());
        if (ssl_ctx == NULL) {
            log_fatal("SSL_CTX_new error, %s", ERR_error_string(ERR_get_error(), NULL));
        }
        SSL_CTX_set_options(ssl_ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2);
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_RELEASE_BUFFERS);
        SSL_CTX_set_next_proto_select_cb(ssl_ctx, select_next_proto_cb, NULL);

        ssl = SSL_new(ssl_ctx);
        if (ssl == NULL) {
            log_fatal("SSL_new error, %s", ERR_error_string(ERR_get_error(), NULL));
        }

        ssl_handshake();
    }

    set_sock_option(sockfd);

    nghttp2_session_callbacks *callbacks = NULL;
    int rc = nghttp2_session_callbacks_new(&callbacks);
    if (rc) {
        log_fatal("nghttp2_session_callbacks_new return <%d>", rc);
    }
    nghttp2_session_callback_setup(callbacks);

    nghttp2_option *option = NULL;
    nghttp2_option_new(&option);
    nghttp2_option_set_builtin_recv_extension_type(option, NGHTTP2_ALTSVC);
    nghttp2_option_set_builtin_recv_extension_type(option, NGHTTP2_ORIGIN);

    rc = nghttp2_session_client_new2(&sess, callbacks, NULL, option);
    if (rc) {
        log_fatal("nghttp2_session_client_new return %d", rc);
    }

    nghttp2_session_callbacks_del(callbacks);

    submit_settings();
    submit_request(uri);

    run_event_loop();

    nghttp2_session_del(sess);
    nghttp2_option_del(option);
    close(sockfd);
    SSL_free(ssl);
    SSL_CTX_free(ssl_ctx);
}

int
main(int argc, char **argv)
{
    register_signal_handlers();
    openssl_init();

    if (argc < 2) {
        log_fatal("%s uri", basename(argv[0]));
    }

    URI *uri = parse_uri(argv[1]);
    if (uri == NULL) {
        log_fatal("parse %s failed", argv[1]);
    }

    fetch_uri(uri);
    free_uri(uri);
    return 0;
}
