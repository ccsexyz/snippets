#include "util.h"

typedef struct {
    int compressed;
    key_value_t *headers;
    key_value_t *allow_header;
    char *bin_path;
    char *file_name;
} config;

static command_t cmds[] = { { "H", "", cmd_set_strlist, offsetof(config, headers), NULL },
    { "", "bin", cmd_set_str, offsetof(config, bin_path), "ffmpeg" },
    { "", "allow_header", cmd_set_strlist, offsetof(config, allow_header), NULL },
    { "", "file_name", cmd_set_str, offsetof(config, file_name), "1.mp4" },
    { "", "compressed", NULL, offsetof(config, compressed), "" } };

int
should_ignore_this_header(config *cfg, const char *key)
{
    if (str_empty(key)) {
        return 1;
    }

    if (cfg && cfg->allow_header) {
        int has_allow_header = 0;

        for (key_value_t *p = cfg->allow_header; p; p = p->next) {
            if (str_empty(p->key)) {
                continue;
            }

            has_allow_header = 1;

            if (!strstr(key, p->key)) {
                return 1;
            }
        }

        return !has_allow_header;
    }

    return 1;
}

char *
build_headers(config *cfg, key_value_t *kv)
{
    size_t len = 0;
    size_t num = 0;

    for (key_value_t *p = kv; p; p = p->next) {
        if (should_ignore_this_header(cfg, p->key)) {
            continue;
        }

        len += strlen(p->key);
        num++;
    }

    if (len == 0) {
        return "";
    }

    int is_first_header = 1;
    char *str = (char *)calloc(1, len + (num + 1) * 5);
    char *orig_str = str;

    for (key_value_t *p = kv; p; p = p->next) {
        if (should_ignore_this_header(cfg, p->key)) {
            continue;
        }

        if (is_first_header) {
            is_first_header = 0;
        } else {
            memcpy(str, "\\r\\n", 4);
            str += 4;
        }

        size_t keylen = strlen(p->key);
        memcpy(str, p->key, keylen);
        str += keylen;
        *str = '\0';
    }

    return orig_str;
}

int
main(int argc, const char **argv)
{
    char *errstr = NULL;
    char *url = NULL;
    config *cfg = (config *)calloc(1, sizeof(config));

    int rc = parse_command_args(argc, argv, cfg, cmds, array_size(cmds), &errstr, &url);
    if (rc != 0) {
        log_fatal("parse command error: %s", errstr);
    }
    free(errstr);

    if (str_empty(url)) {
        log_fatal("no url");
    }

    char *headers = build_headers(cfg, cfg->headers);

    log_info("url %s", url);
    log_info("headers %s", headers);

    rc = execlp(cfg->bin_path, basename(cfg->bin_path), "-headers", headers, "-i", url, "-c",
        "copy", cfg->file_name, NULL);
    if (rc < 0) {
        log_fatal("execl error: %s", strerror(errno));
    }

    return 0;
}
