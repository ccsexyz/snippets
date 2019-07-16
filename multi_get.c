#include "util.h"
#include <curl/curl.h>

int still_alive = 1;

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
} file_context_t;

struct curl_context_s {
    int naive;
    int probing;
    int part_index;
    size_t offset;
    file_context_t *file_ctx;
};

static size_t size_of_part(file_context_t *file_ctx, int n) {
    assert(file_ctx->part_size > 0);

    if (file_ctx->part_size * (n + 1) > file_ctx->content_length) {
        return file_ctx->content_length - file_ctx->part_size * n;
    }

    return file_ctx->part_size;
}

static size_t dummy_write_cb(char *data, size_t n, size_t l, void *userp)
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

static size_t write_cb(char *data, size_t n, size_t l, void *userp)
{
    /* take care of the data here, ignored in this example */ 
    curl_context_t *curl_ctx = (curl_context_t *)userp;
    file_context_t *file_ctx = curl_ctx->file_ctx;

    size_t nwrite = n * l;
    if (curl_ctx->naive == 0 && nwrite + curl_ctx->offset > (curl_ctx->part_index + 1) * file_ctx->part_size > file_ctx->content_length) {
        nwrite = file_ctx->content_length - curl_ctx->part_index * file_ctx->part_size;
    }

    if (nwrite > 0) {
        pwrite(file_ctx->fd, data, nwrite, curl_ctx->offset);
        curl_ctx->offset += nwrite;
    }

    return n*l;
}

static void process_content_range_header(char *str, curl_context_t *curl_ctx) {
    if (strncasecmp(str, "bytes 0-0/", strlen("bytes 0-0/"))) {
        return;
    }

    str += strlen("bytes 0-0/");

    if (strstr(str, "*")) {
        return;
    }

    curl_ctx->file_ctx->content_length = strtol(str, NULL, 10);
}

static size_t probe_header_callback(char *data, size_t size, size_t nitems, void *userdata) {
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

static void add_range_header(CURL *eh, size_t start, size_t end) {
    char range_buf[1024];
    snprintf(range_buf, sizeof(range_buf), "%ld-%ld", start, end);
    curl_easy_setopt(eh, CURLOPT_RANGE, range_buf);
}

static const char *get_file_name(const char *url) {
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

static void add_probe_transfer(CURLM *cm, file_context_t *file_ctx) {
    const char *url = file_ctx->url;

    curl_context_t *curl_ctx = (curl_context_t *)calloc(sizeof(curl_context_t), 1);
    curl_ctx->file_ctx = file_ctx;
    curl_ctx->probing = 1;

    CURL *eh = curl_easy_init();
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, dummy_write_cb);
    curl_easy_setopt(eh, CURLOPT_URL, url);
    curl_easy_setopt(eh, CURLOPT_PRIVATE, curl_ctx);
    curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, probe_header_callback);
    curl_easy_setopt(eh, CURLOPT_HEADERDATA, curl_ctx);
    add_range_header(eh, 0, 0);
    curl_multi_add_handle(cm, eh);

    still_alive = 1;
}

static void add_transfer(CURLM *cm, curl_context_t *curl_ctx) {
    file_context_t *file_ctx = curl_ctx->file_ctx;

    CURL *eh = curl_easy_init();
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, curl_ctx);
    curl_easy_setopt(eh, CURLOPT_URL, file_ctx->url);
    curl_easy_setopt(eh, CURLOPT_PRIVATE, curl_ctx);

    if (!curl_ctx->naive) {
        size_t start = curl_ctx->offset;
        size_t end = start + size_of_part(file_ctx, curl_ctx->part_index) - 1;
        add_range_header(eh, start, end);
    }

    curl_multi_add_handle(cm, eh);

    still_alive = 1;
}

static void naive_get(CURLM *cm, file_context_t *file_ctx) {
    curl_context_t *curl_ctx = (curl_context_t *)calloc(sizeof(curl_context_t), 1);
    curl_ctx->file_ctx = file_ctx;
    curl_ctx->naive = 1;

    add_transfer(cm, curl_ctx);
}

static void update_progress(CURLM *cm, file_context_t *file_ctx) {
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

static void process_probing_handle(CURLM *cm, CURL *e, curl_context_t *curl_ctx) {
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

static void process_work_handle(CURLM *cm, CURL *e, curl_context_t *curl_ctx) {
    curl_ctx->file_ctx->work_num--;
    update_progress(cm, curl_ctx->file_ctx);
}

static void process_naive_handle(CURLM *cm, CURL *e, curl_context_t *curl_ctx) {
}

static void process_completed_handle(CURLM *cm, CURL *e) {
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

static void process_message(CURLM *cm, CURLMsg *msg) {
    if(msg->msg == CURLMSG_DONE) {
        curl_context_t *curl_ctx = NULL;
        CURL *e = msg->easy_handle;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &curl_ctx);
        assert(curl_ctx);
        fprintf(stderr, "R: %d - %s <%s> part_index=%d\n",
                msg->data.result, curl_easy_strerror(msg->data.result), curl_ctx->file_ctx->url, curl_ctx->part_index);
        process_completed_handle(cm, e);
        curl_multi_remove_handle(cm, e);
        curl_easy_cleanup(e);
    } else {
        fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
    }
}

static file_context_t *create_file_context(const char *url) {
    file_context_t *file_ctx = (file_context_t *)calloc(sizeof(file_context_t), 1);
    file_ctx->url = strdup(url);
    file_ctx->file_name = get_file_name(url);
    file_ctx->part_size = 2 * 1048576;
    file_ctx->concurrency = 8;

    return file_ctx;
}

static void reset_str_ptr(char **pstr, char *new) {
    free(*pstr);
    *pstr = strdup(new);
}

static void file_context_parse(file_context_t *file_ctx, key_value_t *kv) {
    for (key_value_t *p = kv; p; p = p->next) {
        int num = atoi(p->value);

        if (!strcasecmp(p->key, "file_name")) {
            reset_str_ptr((char **)&file_ctx->file_name, p->value);
        } else if (!strcasecmp(p->key, "part_size")) {
            file_ctx->part_size = num;
        } else if (!strcasecmp(p->key, "conn") && num > 0) {
            file_ctx->concurrency = num;
        }
    }
}

int main(int argc, const char *argv[]) {
    const char *base = basename(strdup(argv[0]));

    if (argc < 2) {
        printf("usage: %s url\n", base);
        return 1;
    }

    key_value_t *kv = parse_key_values_from_str_array(&argv[2], argc - 2);
    file_context_t *file_ctx = create_file_context(argv[1]);
    file_context_parse(file_ctx, kv);

    file_ctx->fd = open(file_ctx->file_name, O_WRONLY | O_CREAT, 0644);

    if (file_ctx->fd < 0) {
        printf("open %s error: %s\n", file_ctx->file_name, strerror(errno));
        exit(EXIT_FAILURE);
    }

    CURLM *cm;
    CURLMsg *msg;
    unsigned int transfers = 0;
    int msgs_left = -1;

    curl_global_init(CURL_GLOBAL_ALL);
    cm = curl_multi_init();

    add_probe_transfer(cm, file_ctx);

    do {
        curl_multi_perform(cm, &still_alive);

        while((msg = curl_multi_info_read(cm, &msgs_left))) {
            process_message(cm, msg);
        }

        if (still_alive) {
            curl_multi_wait(cm, NULL, 0, 1000, NULL);
        }
    } while(still_alive);

    fsync(file_ctx->fd);

    curl_multi_cleanup(cm);
    curl_global_cleanup();

    return EXIT_SUCCESS;
}
