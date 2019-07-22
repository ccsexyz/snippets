#include "util.h"
#include <uv.h>

struct config {
    int local_port;
} conf;

static command_t cmds[] = {
    {
        "l",
        "local_port",
        cmd_set_int,
        offsetof(struct config, local_port),
        "6090"
    }
};

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

static uv_loop_t *default_loop;

static void on_close(uv_handle_t *handle) {
    free(handle);
}

static void after_shutdown(uv_shutdown_t *req, int status) {
    uv_close((uv_handle_t *)req->handle, on_close);
    free(req);
}

static void after_write(uv_write_t *req, int status) {
    write_req_t *wr = (write_req_t *)req;

    free(wr->buf.base);
    free(wr);

    if (status == 0) {
        return;
    }

    if (status == UV_ECANCELED) {
        return;
    }

    uv_close((uv_handle_t *)req->handle, on_close);
}

static void after_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    if (nread <= 0 && buf->base) {
        free(buf->base);
    }

    if (nread == 0) {
        return;
    }

    if (nread < 0) {
        uv_shutdown_t *req = (uv_shutdown_t *)malloc(sizeof(uv_shutdown_t));
        assert(req);

        int rc = uv_shutdown(req, handle, after_shutdown);
        assert(rc == 0);

        return;
    }

    write_req_t *wr = (write_req_t *)malloc(sizeof(write_req_t));
    assert(wr);

    wr->buf = uv_buf_init(buf->base, nread);

    int rc = uv_write(&wr->req, handle, &wr->buf, 1, after_write);
    assert(rc == 0);
}

static void alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    buf->base = malloc(suggested_size);
    assert(buf->base);
    buf->len = suggested_size;
}

static void on_new_connection(uv_stream_t *server, int status) {
    assert(status == 0);

    uv_tcp_t *stream = calloc(1, sizeof(uv_tcp_t));
    assert(stream);

    int rc = uv_tcp_init(default_loop, stream);
    assert(rc == 0);

    stream->data = server;

    rc = uv_accept(server, (uv_stream_t *)stream);
    assert(rc == 0);

    rc = uv_read_start((uv_stream_t *)stream, alloc_cb, after_read);
    assert(rc == 0);
}

static int create_tcp_echo_server(uv_loop_t *loop, int port) {
    struct sockaddr_in addr;

    int rc = uv_ip4_addr("127.0.0.1", port, &addr);
    if (rc != 0) {
        return rc;
    }

    uv_tcp_t *server = (uv_tcp_t *)calloc(1, sizeof(uv_tcp_t));
    assert(server);

    rc = uv_tcp_init(loop, server);
    if (rc != 0) {
        free(server);
        return rc;
    }

    rc = uv_tcp_bind(server, (const struct sockaddr *)&addr, 0);
    assert(rc == 0);

    rc = uv_listen((uv_stream_t *)server, 512, on_new_connection);
    assert(rc == 0);

    return 0;
}

int main(int argc, char **argv) {
    uv_loop_t *loop = uv_default_loop();
    assert(loop);
    default_loop = loop;

    char *errstr = NULL;
    int rc = parse_command_args(argc, (const char **)argv, &conf, cmds, array_size(cmds), &errstr, NULL);
    if (rc != 0) {
        log_fatal("parse command error: %s", errstr ? errstr : "");
    }
    free(errstr);

    rc = create_tcp_echo_server(loop, conf.local_port);
    assert(rc == 0);

    rc = uv_run(loop, UV_RUN_DEFAULT);
    assert(rc == 0);

    return 0;
}

