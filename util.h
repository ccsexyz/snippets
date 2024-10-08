#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <math.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define array_size(a) (sizeof(a) / sizeof(a[0]))

static int
split_host_port(const char *hostport, char *host, size_t hostlen, int *pport)
{
    if (hostport == NULL || strlen(hostport) <= 1) {
        return 1;
    }

    char *colon = strstr((char *)hostport, ":");
    if (!colon) {
        return 1;
    }

    char *port_str = colon + 1;
    if (*port_str == '\0') {
        return 1;
    }

    size_t len = colon - hostport;
    if (len + 1 > hostlen || host == NULL) {
        return 1;
    }

    memcpy(host, hostport, len);
    host[len] = '\0';

    int port = atoi(port_str);
    if (port > 65535 || port < 0) {
        return 1;
    }

    if (pport) {
        *pport = port;
    }

    return 0;
}

static void
reset_str_ptr(char **pstr, char *new_str)
{
    free(*pstr);
    *pstr = strdup(new_str);
}

static double
tv_sub_msec_double(struct timeval end, struct timeval start)
{
    return ((double)(end.tv_sec - start.tv_sec)) * 1000
        + ((double)(end.tv_usec - start.tv_usec)) / 1000;
}

static long
tv_sub_msec(struct timeval end, struct timeval start)
{
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
}

static double
ts_sub_msec_double(struct timespec end, struct timespec start)
{
    return ((double)(end.tv_sec - start.tv_sec)) * 1000
        + ((double)(end.tv_nsec - start.tv_nsec)) / 1000000;
}

static long
ts_sub_msec(struct timespec end, struct timespec start)
{
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

static struct timeval
tv_now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv;
}

static long
tv_now_msec()
{
    struct timeval tv = tv_now();
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static long
tv_now_usec()
{
    struct timeval tv = tv_now();
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

static void
get_size_str(size_t sz, char *buf, size_t cap)
{
    double n = 0.0;
    const char *unit = "";
    static const size_t kb = 1UL << 10;
    static const size_t mb = 1UL << 20;
    static const size_t gb = 1UL << 30;
    static const size_t tb = 1UL << 40;

    if (sz < kb) {
        n = sz;
    } else if (sz < mb) {
        n = sz / (double)kb;
        unit = "KB";
    } else if (sz < gb) {
        n = sz / (double)mb;
        unit = "MB";
    } else if (sz < tb) {
        n = sz / (double)gb;
        unit = "GB";
    } else {
        n = sz / (double)tb;
        unit = "TB";
    }

    snprintf(buf, cap, "%.1f%s", n, unit);
}

typedef struct key_value_s key_value_t;

struct key_value_s {
    char *value;
    key_value_t *next;
    char key[0];
};

static key_value_t *
parse_key_value(const char *str)
{
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    if (len < 3) {
        return NULL;
    }

    char *equal = strstr((char *)str, "=");
    if (!equal || equal == str || equal == str + len - 1) {
        return NULL;
    }

    key_value_t *kv = (key_value_t *)malloc(sizeof(key_value_t) + len + 1);
    memcpy(kv->key, str, len);

    equal = strstr(kv->key, "=");
    *equal = '\0';
    kv->value = equal + 1;
    kv->next = NULL;

    return kv;
}

static void
free_all_key_values(key_value_t *kv)
{
    while (kv) {
        key_value_t *kv_to_free = kv;
        kv = kv->next;
        free(kv_to_free);
    }
}

static key_value_t *
parse_key_values_from_str_array(const char **str_arr, size_t arr_num)
{
    if (arr_num == 0 || str_arr == NULL) {
        return NULL;
    }

    key_value_t *ret = NULL;

    for (size_t i = arr_num; i > 0; i--) {
        const char *str = str_arr[i - 1];

        if (!str) {
            continue;
        }

        key_value_t *kv = parse_key_value(str);

        if (!kv) {
            continue;
        }

        kv->next = ret;
        ret = kv;
    }

    return ret;
}

static int
str_empty(const char *str)
{
    return str == NULL || strlen(str) == 0;
}

static void
set_errstr(char **errstr, const char *msg)
{
    if (errstr == NULL) {
        return;
    }
    if (msg == NULL) {
        msg = "";
    }
    assert(*errstr == NULL);
    *errstr = strdup(msg);
}

typedef int (*command_set_handler)(void *p, const char *value, char **errstr);
typedef struct command_s command_t;

static int
cmd_set_int(void *p, const char *value, char **errstr)
{
    assert(value);

    *(int *)p = atoi(value);

    return 0;
}

static int
cmd_set_str(void *p, const char *value, char **errstr)
{
    assert(value);

    char *old_str = *(char **)p;
    if (old_str != NULL) {
        free(old_str);
    }
    *(char **)p = strdup(value);

    return 0;
}

static int
cmd_set_strlist(void *p, const char *value, char **errstr)
{
    assert(value);

    size_t len = strlen(value);
    if (len == 0) {
        return 0;
    }

    key_value_t **pkv = (key_value_t **)p;

    key_value_t *kv = (key_value_t *)malloc(sizeof(key_value_t) + len + 1);
    memcpy(kv->key, value, len);
    kv->next = *pkv;
    kv->value = NULL;

    *pkv = kv;

    return 0;
}

static int
cmd_set_bool(void *p, const char *value, char **errstr)
{
    assert(value);

    if (!strcasecmp(value, "on")) {
        *(int *)p = 1;
    } else if (!strcasecmp(value, "off")) {
        *(int *)p = 0;
    } else {
        set_errstr(errstr, "should be on|off");
        return -1;
    }

    return 0;
}

static int
cmd_set_size(void *p, const char *value, char **errstr)
{
    assert(value);

    char *endp = NULL;
    int shift = 0;

    long n = strtol(value, &endp, 0);

    if (endp == value) {
        set_errstr(errstr, "size invalid");
        return -1;
    }

    switch (*endp) {
    case 'T':
    case 't':
        shift = 40;
        break;

    case 'G':
    case 'g':
        shift = 30;
        break;

    case 'M':
    case 'm':
        shift = 20;
        break;

    case 'K':
    case 'k':
        shift = 10;
        break;

    case '\0':
        break;

    default:
        set_errstr(errstr, "unit invalid");
        return -2;
    }

    if (shift > 0) {
        endp++;
    }

    if (*endp != '\0') {
        set_errstr(errstr, "invalid size");
        return -3;
    }

    if (shift != 0 && (n & (~0UL << (64 - shift)))) {
        set_errstr(errstr, "invalid size");
        return -4;
    }

    *(long *)p = n << shift;

    return 0;
}

struct command_s {
    const char *short_name;
    const char *long_name;
    command_set_handler set_handler;
    size_t offset;
    const char *default_value;
    const char *usage;
};

static const char *
name_of_command(command_t *cmd)
{
    return !str_empty(cmd->long_name) ? cmd->long_name : cmd->short_name;
}

static int
check_commands(command_t *cmds, int ncmd, char **errstr)
{
    char errbuf[1024];

    for (int i = 0; i < ncmd; i++) {
        command_t *cmd = &cmds[i];

        if (str_empty(cmd->short_name) && str_empty(cmd->long_name)) {
            snprintf(errbuf, sizeof(errbuf), "the %dth cmd dont have valid name", i);
            goto check_error;
        }
        if (strlen(cmd->short_name) > 1) {
            snprintf(errbuf, sizeof(errbuf), "cmd %s short_name should have only one character",
                name_of_command(cmd));
            goto check_error;
        }
    }

    return 0;

check_error:
    set_errstr(errstr, errbuf);
    return -1;
}

static int
set_default_value_of_commands(void *cfg, command_t *cmds, int ncmd, char **errstr)
{
    char errbuf[1024];

    for (int i = 0; i < ncmd; i++) {
        command_t *cmd = &cmds[i];

        if (cmd->default_value == NULL) {
            continue;
        }

        if (cmd->set_handler == NULL) {
            cmd_set_bool((char *)cfg + cmd->offset, "off", NULL);
            continue;
        }

        char *errstr2 = NULL;
        int rc = cmd->set_handler((char *)cfg + cmd->offset, cmd->default_value, &errstr2);
        if (rc != 0) {
            snprintf(errbuf, sizeof(errbuf), "failed to set default value of command %s: %s",
                name_of_command(cmd), errstr2 ? errstr2 : "");
            set_errstr(errstr, errbuf);
            free(errstr2);
            return rc;
        }
    }

    return 0;
}

static void
build_longopts(command_t *cmds, int ncmd, struct option *longopts, int *pflag)
{
    assert(pflag);

    int optnum = 0;

    for (int i = 0; i < ncmd; i++) {
        command_t *cmd = &cmds[i];

        if (str_empty(cmd->long_name)) {
            continue;
        }

        struct option *opt = &longopts[optnum++];
        opt->name = cmd->long_name;
        opt->has_arg = cmd->set_handler ? required_argument : no_argument;
        if (str_empty(cmd->short_name)) {
            opt->flag = pflag;
            opt->val = i;
        } else {
            opt->flag = NULL;
            opt->val = *cmd->short_name;
        }
    }
}

static char *
build_optstr(command_t *cmds, int ncmd)
{
    size_t cap = 3 * (ncmd + 2);
    char *orig_optstr = (char *)calloc(cap, 1);
    char *optstr = orig_optstr;
    int has_help_command = 0;

    for (int i = 0; i < ncmd; i++) {
        command_t *cmd = &cmds[i];

        if (str_empty(cmd->short_name)) {
            continue;
        }

        if (!strcmp(cmd->short_name, "h")) {
            has_help_command = 1;
        }

        optstr += snprintf(optstr, orig_optstr + cap - optstr, "%s", cmd->short_name);

        if (cmd->set_handler) {
            optstr += snprintf(optstr, orig_optstr + cap - optstr, ":");
        }
    }

    if (!has_help_command) {
        optstr += snprintf(optstr, orig_optstr + cap - optstr, "h");
    }

    return orig_optstr;
}

static void
output_command_usage(command_t *cmds, int ncmd)
{
    int max_long_name_len = 0;

    for (int i = 0; i < ncmd; i++) {
        command_t *cmd = &cmds[i];

        if (str_empty(cmd->long_name)) {
            continue;
        }

        int name_len = strlen(cmd->long_name);
        if (name_len > max_long_name_len) {
            max_long_name_len = name_len;
        }
    }

    for (int i = 0; i < ncmd; i++) {
        command_t *cmd = &cmds[i];

        int has_short_name = !str_empty(cmd->short_name);
        int has_long_name = !str_empty(cmd->long_name);
        int has_usage = !str_empty(cmd->usage);
        int has_default_value = !str_empty(cmd->default_value);

        if (has_short_name) {
            printf("-%s, ", cmd->short_name);
        } else {
            printf("    ");
        }

        if (has_long_name) {
            printf("--%-*s", max_long_name_len + 1, cmd->long_name);
        } else {
            printf("%-*s", max_long_name_len + 3, "");
        }

        if (has_usage) {
            printf("%s", cmd->usage);
        }

        if (has_default_value) {
            printf(" (default: %s)", cmd->default_value);
        }

        printf("\n");
    }
}

static int
parse_command_args(int argc, const char **argv, void *cfg, command_t *cmds, int ncmd, char **errstr,
    char **extra_arg)
{
    int c = 0;
    int rc = 0;

    rc = check_commands(cmds, ncmd, errstr);
    if (rc != 0) {
        return rc;
    }

    rc = set_default_value_of_commands(cfg, cmds, ncmd, errstr);
    if (rc != 0) {
        return rc;
    }

    int flag = 0;
    struct option *longopts = (struct option *)calloc(sizeof(struct option), ncmd + 1);
    build_longopts(cmds, ncmd, longopts, &flag);

    char *optstr = build_optstr(cmds, ncmd);

    char **dup_argv = (char **)calloc(sizeof(char *), argc + 1);
    memcpy(dup_argv, argv, sizeof(const char *) * argc);

    while ((c = getopt_long(argc, dup_argv, optstr, longopts, NULL)) != -1) {
        command_t *cmd = NULL;

        if (c == 0) {
            assert(flag <= ncmd);
            cmd = &cmds[flag];
        } else {
            int found = 0;

            for (int i = 0; i < ncmd; i++) {
                cmd = &cmds[i];

                if (str_empty(cmd->short_name)) {
                    continue;
                }
                if (*cmd->short_name == c) {
                    found = 1;
                    break;
                }
            }

            if (!found) {
                if (c == 'h') {
                    output_command_usage(cmds, ncmd);
                    exit(1);
                }

                rc = -1;
                set_errstr(errstr, "invalid command");
                goto parse_done;
            }
        }
        assert(cmd);

        if (cmd->set_handler) {
            rc = cmd->set_handler((char *)cfg + cmd->offset, optarg, errstr);
            if (rc != 0) {
                goto parse_done;
            }
        } else {
            cmd_set_bool((char *)cfg + cmd->offset, "on", NULL);
        }

        flag = 0;
    }

    if (rc == 0 && optind < argc && extra_arg) {
        reset_str_ptr(extra_arg, dup_argv[optind]);
    }

parse_done:
    free(dup_argv);
    free(longopts);
    free(optstr);

    return rc;
}

static size_t
time_format(char *buf, size_t cap, time_t now)
{
    if (buf == NULL || cap == 0) {
        return 0;
    }

    struct tm _tm;
    localtime_r(&now, &_tm);

    int rc = snprintf(buf, cap, "%d-%02d-%02d %02d:%02d:%02d", _tm.tm_year + 1900, _tm.tm_mon + 1,
        _tm.tm_mday, _tm.tm_hour, _tm.tm_min, _tm.tm_sec);

    if (rc > 0) {
        return (size_t)rc;
    }

    return 0;
}

static int
read_command_output_popen(const char *command, char *buf, size_t n)
{
    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
        return -1;
    }

    for (; n > 1;) {
        size_t nr = fread(buf, 1, n - 1, pipe);

        if (nr > 0) {
            buf[nr] = '\0';
            buf += nr;
            n -= nr;
        }

        if (nr != n - 1) {
            if (feof(pipe) || ferror(pipe)) {
                break;
            }
        }
    }

    pclose(pipe);

    return 0;
}

static char **
split_cstring(const char *str, const char *sep)
{
    if (str_empty(str) || str_empty(sep)) {
        return NULL;
    }

    char *dupped_str = strdup(str);
    size_t lenstr = strlen(dupped_str);
    str = dupped_str;
    char **arr = (char **)malloc(sizeof(char *) * (lenstr + 1));
    char *token = NULL;
    char *ptr = NULL;
    int n = 0;

    while ((token = strtok_r((char *)str, sep, &ptr)) != NULL) {
        str = NULL;
        arr[n++] = strdup(token);
    }

    arr[n] = NULL;
    free(dupped_str);

    return arr;
}

static void
do_read_from_fd(int fd, char *buf, size_t n)
{
    if (n > 0) {
        buf[0] = '\0';
    }

    while (n > 1) {
        buf[0] = '\0';

        int nr = read(fd, buf, n - 1);
        if (nr > 0) {
            buf[nr] = '\0';
            buf += nr;
            n -= nr;
        }

        if (nr <= 0) {
            break;
        }
    }
}

static int
read_command_output_impl_2(char **arr, char *buf, size_t n, int fds[2])
{
    int r = fds[0];
    int w = fds[1];
    int saved_errno = 0;

    pid_t pid = vfork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        // child process
        close(r);

        // redirect stdout and stderr to fd w
        if (dup2(w, 1) < 0 || dup2(w, 2) < 0) {
            saved_errno = errno;
            _Exit(0);
        }

        if (execvp(arr[0], arr) < 0) {
            saved_errno = errno;
            _Exit(1);
        }
    } else {
        // parent, read output from fd r
        int dup_ret = dup2(r, w);
        if (dup_ret >= 0) {
            do_read_from_fd(r, buf, n);
        }

        waitpid(pid, NULL, 0);

        if (dup_ret < 0) {
            return -1;
        }
    }

    return 0;
}

static int
read_command_output_impl_1(const char *command, char *buf, size_t n, int fds[2])
{
    char **arr = split_cstring(command, " ");
    if (arr == NULL) {
        return -1;
    }

    int rc = read_command_output_impl_2(arr, buf, n, fds);

    for (int i = 0; arr[i] != NULL; i++) {
        free(arr[i]);
    }
    free(arr);

    return rc;
}

static int
read_command_output(const char *command, char *buf, size_t n)
{
    if (buf == NULL || n == 0) {
        return -2;
    }

    int fds[2];
    if (pipe(fds) < 0) {
        return -1;
    }

    int rc = read_command_output_impl_1(command, buf, n, fds);

    close(fds[0]);
    close(fds[1]);

    return rc;
}

typedef struct URI URI;

struct URI {
    // all strings are terminated with '\0'
    const char *schema;
    const char *host;
    const char *path;
    uint16_t port;
};

static URI *
parse_uri(const char *uri)
{
    if (str_empty(uri)) {
        return NULL;
    }

    int is_http = 0;
    int is_https = 0;
    const char *schema = uri;
    const char *schema_end = strstr(uri, "://");
    const char *host = NULL;
    size_t schema_len = 0;
    if (schema_end == NULL) {
        schema = "http";
        schema_len = strlen("http");
        host = uri;
        is_http = 1;
    } else {
        schema_len = schema_end - schema;
        is_http = !strncmp(schema, "http", schema_len);
        is_https = !strncmp(schema, "https", schema_len);
        if (!is_http && !is_https) {
            return NULL;
        }
        host = schema_end + strlen("://");
    }

    const char *host_end = NULL;
    const char *path = strstr(host, "/");
    if (path == NULL) {
        path = "/";
        host_end = host + strlen(host);
    } else {
        host_end = path;
    }

    size_t host_len = 0;
    const char *port = NULL;
    size_t port_len = 0;
    if (host[0] == '[') {
        const char *v6_addr = host + 1;
        const char *v6_end = NULL;
        for (const char *p = v6_addr; p < host_end; p++) {
            if (*p == ']') {
                v6_end = p - 1;
                break;
            }
        }
        if (v6_end == NULL) {
            return NULL;
        }
        host = v6_addr;
        host_len = v6_end - host + 1;
        if (v6_end + 1 != host_end) {
            if (v6_end[0] != ':') {
                return NULL;
            }
            port_len = host_end - v6_end - 2;
            if (port_len == 0) {
                return NULL;
            }
            port = v6_end + 2;
        }
    } else {
        for (const char *p = host; p < host_end; p++) {
            if (*p == ':') {
                port = p + 1;
                break;
            }
        }
        if (port) {
            host_len = port - host - 1;
            if (port == host_end) {
                return NULL;
            }
            port_len = host_end - port;
        } else {
            host_len = host_end - host;
        }
    }

    uint16_t uport = is_http ? 80 : 443;
    if (port) {
        char buf[10];
        if (port_len > sizeof(buf) - 1) {
            return NULL;
        }
        memcpy(buf, port, port_len);
        buf[port_len] = '\0';
        char *endp = NULL;
        long lport = strtol(buf, &endp, 10);
        if (endp == NULL || *endp != '\0') {
            return NULL;
        }
        if (lport <= 0 || lport >= 65536) {
            return NULL;
        }
        uport = (uint16_t)lport;
    }

    URI *u = (URI *)malloc(sizeof(URI));
    u->schema = strndup(schema, schema_len);
    u->host = strndup(host, host_len);
    u->path = strdup(path);
    u->port = uport;

    return u;
}

static void
free_uri(URI *uri)
{
    if (uri == NULL) {
        return;
    }

    free((void *)uri->schema);
    free((void *)uri->host);
    free((void *)uri->path);
    free((void *)uri);
}

enum { LOG_VERBOSE, LOG_DEBUG, LOG_INFO, LOG_ERROR, LOG_ALERT, LOG_FATAL, LOG_MAX_LEVEL };

static const char *__log_level_str[] = { "VERBOSE", "DEBUG", "INFO", "ERROR", "ALERT", "FATAL" };

static int
log_level_str_to_int(const char *level_str)
{
    int i = 0;

    for (; i < LOG_MAX_LEVEL; i++) {
        if (!strcasecmp(level_str, __log_level_str[i])) {
            break;
        }
    }

    return i;
}

static const char *
log_level_int_to_str(int level)
{
    if (level < 0 || level >= LOG_MAX_LEVEL) {
        return NULL;
    }

    return __log_level_str[level];
}

static void
log_raw(int logfd, char *buf, size_t cap, int level, const char *file, int line, const char *func,
    const char *fmt, ...)
{
    if (level >= LOG_MAX_LEVEL || level < 0) {
        return;
    }

    if (logfd < 0) {
        return;
    }

    char _buf[4096];
    if (buf == NULL || cap == 0) {
        buf = _buf;
        cap = sizeof(_buf);
    }

    size_t off = 0;
    struct timeval tv = tv_now();

    off += time_format(buf + off, cap - off, tv.tv_sec);
    long tmp_usec = tv.tv_usec;
    off += snprintf(buf + off, cap - off, ".%06ld ", tmp_usec);
    off += snprintf(
        buf + off, cap - off, "[%s] %s:%d, %s, ", __log_level_str[level], file, line, func);

    va_list args;
    va_start(args, fmt);
    off += vsnprintf(buf + off, cap - off, fmt, args);
    va_end(args);

    if (buf[off - 1] != '\n') {
        off += snprintf(buf + off, cap - off, "\n");
    }

    write(logfd, buf, off);
}

#ifndef LOG_MIN_LEVEL
#define LOG_MIN_LEVEL LOG_INFO
#endif

#ifndef TRIM_FILE_NAME
#define TRIM_FILE_NAME(name)                                                                       \
    ({                                                                                             \
        const char *x = strrchr(name, '/');                                                        \
        const char *res = x ? x + 1 : name;                                                        \
        res;                                                                                       \
    })
#endif

#define log__(level, fmt, args...)                                                                   \
    do {                                                                                           \
        if ((level) >= (LOG_MIN_LEVEL))                                                            \
            log_raw(STDOUT_FILENO, NULL, 0, (level), TRIM_FILE_NAME(__FILE__), (__LINE__),         \
                (__func__), (fmt), ##args);                                                        \
    } while (0)

#define log_verb(fmt, args...) log__(LOG_VERBOSE, (fmt), ##args)

#define log_debug(fmt, args...) log__(LOG_DEBUG, (fmt), ##args)

#define log_info(fmt, args...) log__(LOG_INFO, (fmt), ##args)

#define log_error(fmt, args...) log__(LOG_ERROR, (fmt), ##args)

#define log_alert(fmt, args...) log__(LOG_ALERT, (fmt), ##args)

#define log_fatal(fmt, args...)                                                                    \
    do {                                                                                           \
        log__(LOG_FATAL, (fmt), ##args);                                                             \
        exit(1);                                                                                   \
    } while (0)

#ifdef __cplusplus
}

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <tuple>
#include <algorithm>

class elapsed {
public:
    elapsed(const std::string &info)
        : tv_(tv_now())
        , info_(info)
    {
    }
    ~elapsed() { printf("%s taken %.2fms\n", info_.data(), tv_sub_msec_double(tv_now(), tv_)); }

private:
    struct timeval tv_;
    std::string info_;
};

template <typename T>
std::string
get_filt_type_name()
{
    std::string name = typeid(T).name();

    std::string command = "c++filt " + name;
    size_t n = name.length() * 3 + 10;
    char *buf = (char *)malloc(n);
    if (read_command_output(command.c_str(), buf, n) == 0) {
        name = buf;
        name.erase(name.find('\n'));
    }
    free(buf);

    return name;
}

static std::string get_size_str(size_t sz)
{
    char buf[1024];
    get_size_str(sz, buf, sizeof(buf));
    return std::string(buf);
}

#endif
