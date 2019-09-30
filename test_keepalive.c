#include "util.h"

typedef struct {
    const char *host;
    const char *remote_addr;
    int sleep_second;
} config;

static config cfg;

static command_t cmds[] = {
    {
        "",
        "host",
        cmd_set_str,
        offsetof(config, host),
        ""
    },
    {
        "r",
        "remote_addr",
        cmd_set_str,
        offsetof(config, remote_addr),
        ""
    },
    {
        "s",
        "sleep_second",
        cmd_set_int,
        offsetof(config, sleep_second),
        ""
    }
};

static void parse_and_init_config(int argc, const char **argv) {
    char *errstr = NULL;
    int rc = parse_command_args(argc, argv, &cfg, cmds, array_size(cmds), &errstr, NULL);
    if (rc != 0) {
        log_fatal("parse command error: %s", errstr ? errstr : "-");
    }
    free(errstr);

    int has_host = !str_empty(cfg.host);
    int has_remote_addr = !str_empty(cfg.remote_addr);

    if (!has_host && !has_remote_addr) {
        log_fatal("host or remote_addr must have one");
    }

    if (!has_host) {
        cfg.host = cfg.remote_addr;
    }

    if (!has_remote_addr) {
        cfg.remote_addr = cfg.host;
    }
}

static char rcvbuf[1048576];

static int do_request(int sockfd) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "HEAD / HTTP/1.1\r\nConnection: Keep-Alive\r\nHost: %s\r\n\r\n", cfg.host);
    int rc = write(sockfd, buf, n);
    if (rc != n) {
        log_info("write to sockfd %d error, return %d, error: %s", sockfd, rc, strerror(errno));
        return 1;
    }

    log_info("start to wait response");

    int off = 0;
    while (1) {
        rc = read(sockfd, rcvbuf + off, sizeof(rcvbuf) - off);
        if (rc <= 0) {
            log_info("peer close connection, error: %s", strerror(errno));
            return 1;
        }
        off += rc;
        if (memmem(rcvbuf, off, "\r\n\r\n", strlen("\r\n\r\n"))) {
            // got \r\n\r\n, return ok
            break;
        }
    }

    log_info("got response");

    return 0;
}

static void do_wait(int sockfd) {
    char buf[4];
    log_info("wait start!");
    read(sockfd, buf, sizeof(buf));
    log_info("wait stop!");
}

static void print_peer_info(int sockfd) {
    struct sockaddr_storage storage;
    socklen_t slt = sizeof(storage);
    int rc = getpeername(sockfd, (struct sockaddr *)&storage, &slt);

    if (rc != 0) {
        log_info("getpeername error, reason %s", strerror(errno));
        return;
    }

    char ip[128];
    int port;

    if (storage.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&storage;
        inet_ntop(AF_INET, &s->sin_addr.s_addr, ip, sizeof(ip));
        port = ntohs(s->sin_port);
    } else if (storage.ss_family == AF_INET6) {
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&storage;
        inet_ntop(AF_INET6, &s6->sin6_addr.s6_addr, ip, sizeof(ip));
        port = ntohs(s6->sin6_port);
    } else {
        log_info("invalid ss_family %d", storage.ss_family);
        return;
    }

    log_info("getpeername return ip %s port %u", ip, port);
}

static void test_keepalive() {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;

    char host[1024];
    char port[128];

    char *last_colon = strrchr(cfg.remote_addr, ':');
    if (last_colon == NULL) {
        snprintf(host, sizeof(host), "%s", cfg.remote_addr);
        snprintf(port, sizeof(port), "80");
    } else {
        int host_len = last_colon - cfg.remote_addr;
        host_len = host_len + 1 > sizeof(host) ? sizeof(host) - 1 : host_len;
        snprintf(host, host_len + 1, "%s", cfg.remote_addr);
        snprintf(port, sizeof(port), "%s", last_colon + 1);
    }

    log_info("start to resolve %s:%s", host, port);

    int ec = getaddrinfo(host, port, &hints, &res);
    if (ec) {
        log_fatal("getaddrinfo error: %s", gai_strerror(ec));
    }

    char ip[128];

    if (res->ai_family == AF_INET) {
        inet_ntop(res->ai_family, &((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr, ip, sizeof(ip));
    } else if (res->ai_family == AF_INET6) {
        inet_ntop(res->ai_family, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr.s6_addr, ip, sizeof(ip));
    } else {
        snprintf(ip, sizeof(ip), "-");
    }

    log_info("resolve done, choose ip %s", ip);

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (sockfd < 0) {
        log_fatal("socket create failed: %s", strerror(errno));
    }

    log_info("connect to %s:%s ...", ip, port);

    int rc = connect(sockfd, res->ai_addr, res->ai_addrlen);
    if (rc < 0) {
        log_fatal("connect failed: %s", strerror(errno));
    }

    log_info("connect done, start to send request");

    rc = do_request(sockfd);
    if (rc == 0) {
        do_wait(sockfd);
    }

    if (cfg.sleep_second > 0) {
        sleep(cfg.sleep_second);
        print_peer_info(sockfd);
    }

    close(sockfd);
    freeaddrinfo(res);
}

int main(int argc, const char *argv[]) {
    parse_and_init_config(argc, argv);
    test_keepalive();
    return 0;
}
