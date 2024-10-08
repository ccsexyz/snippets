#define LOG_MIN_LEVEL log_level
#include "util.h"
#include <curl/curl.h>

int still_alive = 1;
static int log_level = LOG_INFO;

typedef struct curl_context_s curl_context_t;

typedef struct {
    int fd;
    const char *url;
    const char *file_name;
    size_t content_length;
    size_t part_size;
    int work_num;
    curl_context_t *contexts;
    int context_num;
    int concurrency;
    key_value_t *headers;
    size_t downloaded_length;
    long last_msec;
    size_t last_downloaded_length;
    long speed;
    long remain;
    const char *log_level_str;
    int follow_redirection;
    int max_follow_times;
    int compressed;
} file_context_t;

struct curl_context_s {
    int naive;
    int probing;
    int part_index;
    size_t offset;
    file_context_t *file_ctx;
    double dlnow;
};

static command_t cmds[] = { { "o", "file_name", cmd_set_str, offsetof(file_context_t, file_name),
                                "",
                                "set the filename of output file, if not set, set by url path" },
    { "", "part_size", cmd_set_size, offsetof(file_context_t, part_size), "2M", "range part size" },
    { "", "compressed", NULL, offsetof(file_context_t, compressed), "",
        "add compressed accept-encoding header" },
    { "c", "conn", cmd_set_int, offsetof(file_context_t, concurrency), "8",
        "number of concurrent connections" },
    { "", "log-level", cmd_set_str, offsetof(file_context_t, log_level_str), "INFO", "log level" },
    { "H", "header", cmd_set_strlist, offsetof(file_context_t, headers), "",
        "add customized HTTP Header" },
    { "l", "", NULL, offsetof(file_context_t, follow_redirection), "",
        "enable follow redirection" },
    { "", "max-follow", cmd_set_int, offsetof(file_context_t, max_follow_times), "-1",
        "max follow times, less than zero means no limit" } };

static size_t
size_of_part(file_context_t *file_ctx, int n)
{
    assert(file_ctx->part_size > 0);

    if (file_ctx->part_size * (n + 1) > file_ctx->content_length) {
        return file_ctx->content_length - file_ctx->part_size * n;
    }

    return file_ctx->part_size;
}

static size_t
dummy_write_cb(char *data, size_t n, size_t l, void *userp)
{
    /* take care of the data here, ignored in this example */
    (void)data;
    (void)userp;

    n *= l;
    if (n > 2048) {
        return 0;
    }
    return n;
}

static size_t
write_cb(char *data, size_t n, size_t l, void *userp)
{
    /* take care of the data here, ignored in this example */
    curl_context_t *curl_ctx = (curl_context_t *)userp;
    file_context_t *file_ctx = curl_ctx->file_ctx;

    size_t nwrite = n * l;
    if (curl_ctx->naive == 0
        && nwrite + curl_ctx->offset > (curl_ctx->part_index + 1) * file_ctx->part_size
            > file_ctx->content_length) {
        nwrite = file_ctx->content_length - curl_ctx->part_index * file_ctx->part_size;
    }

    if (nwrite > 0) {
        pwrite(file_ctx->fd, data, nwrite, curl_ctx->offset);
        curl_ctx->offset += nwrite;
    }

    return n * l;
}

static void
process_content_range_header(char *str, curl_context_t *curl_ctx)
{
    if (strncasecmp(str, "bytes 0-0/", strlen("bytes 0-0/"))) {
        return;
    }

    str += strlen("bytes 0-0/");

    if (strstr(str, "*")) {
        return;
    }

    curl_ctx->file_ctx->content_length = strtol(str, NULL, 10);
}

static size_t
probe_header_callback(char *data, size_t size, size_t nitems, void *userdata)
{
    char *orig_str = strndup(data, size * nitems);
    char *str = orig_str;
    size_t content_range_len = strlen("Content-Range:");

    if (!strncasecmp(str, "Content-Range:", content_range_len)) {
        str += content_range_len;

        while (*str == ' ') {
            str++;
        }

        process_content_range_header(str, (curl_context_t *)userdata);
    }

    free(orig_str);
    return nitems * size;
}

static int
progress_callback(void *ctx, double dltotal, double dlnow, double ultotal, double ulnow)
{
    curl_context_t *curl_ctx = (curl_context_t *)ctx;
    file_context_t *file_ctx = curl_ctx->file_ctx;
    file_ctx->downloaded_length += dlnow - curl_ctx->dlnow;
    curl_ctx->dlnow = dlnow;
    return 0;
}

static void
add_range_header(CURL *eh, size_t start, size_t end)
{
    char range_buf[1024];
    snprintf(range_buf, sizeof(range_buf), "%ld-%ld", start, end);
    curl_easy_setopt(eh, CURLOPT_RANGE, range_buf);
}

static const char *
get_file_name(const char *url)
{
    char *dup_url = strdup(url);
    char *base = basename(dup_url);
    char *p = strstr(base, "?");
    if (p) {
        *p = '\0';
    }
    const char *file_name = strdup(base);
    free(dup_url);
    return file_name;
}

static int
check_if_header_should_ignore(const char *header_name)
{
    int rc = 0;

    // ignore range header
    if (!strncasecmp(header_name, "Range:", strlen("Range:"))) {
        rc = 1;
    }

    return rc;
}

static void
add_customized_headers(CURL *eh, key_value_t *headers)
{
    struct curl_slist *chunk = NULL;

    for (key_value_t *p = headers; p; p = p->next) {
        if (check_if_header_should_ignore(p->key)) {
            continue;
        }

        chunk = curl_slist_append(chunk, p->key);
    }

    if (chunk) {
        curl_easy_setopt(eh, CURLOPT_HTTPHEADER, chunk);
        // FIXME memory leak!
        // should call curl_slist_free_all after request finished
    }
}

static void
add_transfer(CURLM *cm, curl_context_t *curl_ctx)
{
    file_context_t *file_ctx = curl_ctx->file_ctx;

    CURL *eh = curl_easy_init();
    curl_easy_setopt(eh, CURLOPT_URL, file_ctx->url);
    curl_easy_setopt(eh, CURLOPT_PRIVATE, curl_ctx);
    add_customized_headers(eh, file_ctx->headers);

    if (file_ctx->follow_redirection) {
        curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(eh, CURLOPT_MAXREDIRS, file_ctx->max_follow_times);
    }

    if (file_ctx->compressed) {
        curl_easy_setopt(eh, CURLOPT_ACCEPT_ENCODING, "deflate, gzip");
    }

    if (curl_ctx->probing) {
        curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, dummy_write_cb);
        curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, probe_header_callback);
        curl_easy_setopt(eh, CURLOPT_HEADERDATA, curl_ctx);
        add_range_header(eh, 0, 0);
    } else {
        curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(eh, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt(eh, CURLOPT_PROGRESSFUNCTION, progress_callback);
        curl_easy_setopt(eh, CURLOPT_PROGRESSDATA, curl_ctx);

        if (!curl_ctx->naive) {
            size_t start = curl_ctx->offset;
            size_t end = start + size_of_part(file_ctx, curl_ctx->part_index) - 1;
            add_range_header(eh, start, end);
        }
    }

    curl_easy_setopt(eh, CURLOPT_WRITEDATA, curl_ctx);

    curl_multi_add_handle(cm, eh);

    still_alive = 1;
}

static void
add_probe_transfer(CURLM *cm, file_context_t *file_ctx)
{
    const char *url = file_ctx->url;

    curl_context_t *curl_ctx = (curl_context_t *)calloc(sizeof(curl_context_t), 1);
    curl_ctx->file_ctx = file_ctx;
    curl_ctx->probing = 1;

    add_transfer(cm, curl_ctx);
}

static void
naive_get(CURLM *cm, file_context_t *file_ctx)
{
    curl_context_t *curl_ctx = (curl_context_t *)calloc(sizeof(curl_context_t), 1);
    curl_ctx->file_ctx = file_ctx;
    curl_ctx->naive = 1;

    add_transfer(cm, curl_ctx);
}

static void
update_progress(CURLM *cm, file_context_t *file_ctx)
{
    if (file_ctx->work_num >= file_ctx->concurrency) {
        return;
    }
    if (file_ctx->context_num == 0) {
        return;
    }

    curl_context_t *curl_ctx = &file_ctx->contexts[file_ctx->context_num - 1];
    file_ctx->context_num--;
    file_ctx->work_num++;
    add_transfer(cm, curl_ctx);

    update_progress(cm, file_ctx);
}

static void
process_probing_handle(CURLM *cm, CURL *e, curl_context_t *curl_ctx)
{
    long response_code = 0;
    curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &response_code);

    file_context_t *file_ctx = curl_ctx->file_ctx;
    if (response_code != 206 || file_ctx->content_length <= file_ctx->part_size) {
        naive_get(cm, curl_ctx->file_ctx);
        return;
    }

    int context_num = file_ctx->content_length / file_ctx->part_size;
    if (file_ctx->content_length % file_ctx->part_size != 0) {
        context_num++;
    }

    file_ctx->contexts = (curl_context_t *)calloc(sizeof(curl_context_t), context_num);
    file_ctx->context_num = context_num;

    for (int i = context_num, j = 0; i > 0; i--, j++) {
        curl_context_t *curl_ctx = &file_ctx->contexts[i - 1];
        curl_ctx->part_index = j;
        curl_ctx->file_ctx = file_ctx;
        curl_ctx->offset = j * file_ctx->part_size;
    }

    update_progress(cm, file_ctx);
}

static void
process_work_handle(CURLM *cm, CURL *e, curl_context_t *curl_ctx)
{
    curl_ctx->file_ctx->work_num--;
    update_progress(cm, curl_ctx->file_ctx);
}

static void
process_naive_handle(CURLM *cm, CURL *e, curl_context_t *curl_ctx)
{
}

static void
process_completed_handle(CURLM *cm, CURL *e)
{
    curl_context_t *curl_ctx = NULL;
    curl_easy_getinfo(e, CURLINFO_PRIVATE, (char **)&curl_ctx);
    assert(curl_ctx);

    if (curl_ctx->probing) {
        process_probing_handle(cm, e, curl_ctx);
    } else if (curl_ctx->naive) {
        process_naive_handle(cm, e, curl_ctx);
    } else {
        process_work_handle(cm, e, curl_ctx);
    }
}

static void
process_message(CURLM *cm, CURLMsg *msg)
{
    if (msg->msg == CURLMSG_DONE) {
        curl_context_t *curl_ctx = NULL;
        CURL *e = msg->easy_handle;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &curl_ctx);
        assert(curl_ctx);
        log_debug("R: %d - %s <%s> part_index=%d\n", msg->data.result,
            curl_easy_strerror(msg->data.result), curl_ctx->file_ctx->url, curl_ctx->part_index);
        process_completed_handle(cm, e);
        curl_multi_remove_handle(cm, e);
        curl_easy_cleanup(e);
    } else {
        log_info("E: CURLMsg (%d)\n", msg->msg);
    }
}

static file_context_t *
create_file_context()
{
    file_context_t *file_ctx = (file_context_t *)calloc(sizeof(file_context_t), 1);

    return file_ctx;
}

static void
print_status(file_context_t *file_ctx)
{
    if (file_ctx->content_length == 0) {
        return;
    }

    long now_msec = tv_now_msec();

    if (file_ctx->last_msec) {
        long msec_taken = now_msec - file_ctx->last_msec;

        if (file_ctx->speed == 0) {
            if (msec_taken < 100) {
                return;
            }
        } else {
            if (msec_taken < 1000) {
                return;
            }
        }

        if (file_ctx->downloaded_length > 0 && msec_taken > 0) {
            size_t download_length = file_ctx->downloaded_length - file_ctx->last_downloaded_length;
            size_t remain_length = file_ctx->content_length - file_ctx->downloaded_length;

            file_ctx->speed = download_length / msec_taken;
            file_ctx->remain = remain_length / (file_ctx->speed + 1);
        }
    }

    file_ctx->last_msec = now_msec;
    file_ctx->last_downloaded_length = file_ctx->downloaded_length;

    char dlnow[1024];
    char dltotal[1024];
    get_size_str(file_ctx->downloaded_length, dlnow, sizeof(dlnow));
    get_size_str(file_ctx->content_length, dltotal, sizeof(dltotal));
    char speed[1024];
    get_size_str(file_ctx->speed * 1000, speed, sizeof(speed));

    log_info("progress: %.2f%% %s/%s speed: %s/s, remain: %lds",
        (double)file_ctx->downloaded_length / file_ctx->content_length * 100, dlnow, dltotal,
        speed, file_ctx->remain / 1000);
}

int
main(int argc, const char *argv[])
{
    const char *base = basename(strdup(argv[0]));

    if (argc < 2) {
        log_alert("usage: %s url", base);
        return 1;
    }

    char *errstr = NULL;
    file_context_t *file_ctx = create_file_context();
    int rc = parse_command_args(
        argc, argv, file_ctx, cmds, array_size(cmds), &errstr, (char **)&file_ctx->url);
    if (rc != 0) {
        log_fatal("parse command error: %s", errstr ? errstr : "");
    }
    free(errstr);

    if (!str_empty(file_ctx->log_level_str)) {
        int level = log_level_str_to_int(file_ctx->log_level_str);
        if (level >= 0 && level < LOG_MAX_LEVEL) {
            log_level = level;
        }
    }

    if (str_empty(file_ctx->url)) {
        log_fatal("no url");
    }

    if (str_empty(file_ctx->file_name)) {
        file_ctx->file_name = get_file_name(file_ctx->url);
    }

    if (file_ctx->concurrency <= 0) {
        log_fatal("concurrency %d must > 0", file_ctx->concurrency);
    }

    file_ctx->fd = open(file_ctx->file_name, O_WRONLY | O_CREAT, 0644);

    if (file_ctx->fd < 0) {
        log_fatal("open %s error: %s\n", file_ctx->file_name, strerror(errno));
    }

    CURLM *cm;
    CURLMsg *msg;
    int msgs_left = -1;

    curl_global_init(CURL_GLOBAL_ALL);
    cm = curl_multi_init();

    add_probe_transfer(cm, file_ctx);

    do {
        curl_multi_perform(cm, &still_alive);

        while ((msg = curl_multi_info_read(cm, &msgs_left))) {
            process_message(cm, msg);
        }

        print_status(file_ctx);

        if (still_alive) {
            curl_multi_wait(cm, NULL, 0, 1000, NULL);
        }
    } while (still_alive);

    fsync(file_ctx->fd);

    curl_multi_cleanup(cm);
    curl_global_cleanup();

    return EXIT_SUCCESS;
}
