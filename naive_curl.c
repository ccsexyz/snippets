#include <curl/curl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int
progress_callback(void *ctx, double dltotal, double dlnow, double ultotal, double ulnow)
{
    // printf("ctx = %p, dltotal = %g dlnow = %g ultotal = %g ulnow = %g\n", ctx, dltotal, dlnow,
    // ultotal, ulnow);
    fflush(stdout);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("[%ld:%06ld] progress %.2f %%\n", tv.tv_sec, tv.tv_usec, dlnow / dltotal * 100.0);
    fflush(stdout);
    return 0;
}

static size_t
header_callback(char *data, size_t size, size_t nitems, void *userdata)
{
    char *str = strndup(data, size * nitems);
    printf("Header: %s\n", str);
    free(str);
    return nitems * size;
}

static int
start_with(const char *str, const char *expect_start)
{
    if (expect_start == NULL) {
        return 1;
    }

    size_t expect_len = strlen(expect_start);
    if (expect_len == 0) {
        return 1;
    }

    if (str == NULL || strlen(str) < expect_len) {
        return 0;
    }

    return !strncmp(str, expect_start, expect_len);
}

int
main(int argc, char **argv)
{
    curl_global_init(CURL_GLOBAL_SSL);

    const char *name = basename(argv[0]);
    if (argc != 2) {
        printf("usage: %s URL\n", name);
        return 1;
    }
    const char *url = argv[1];
    if (!start_with(url, "http://") && !start_with(url, "https://")) {
        printf("url %s not start with http:// or https://\n", url);
        return 1;
    }

    CURL *naive_handle = curl_easy_init();
    if (!naive_handle) {
        printf("curl_easy_init failed\n");
        return 1;
    }

    curl_easy_setopt(naive_handle, CURLOPT_URL, url);
    curl_easy_setopt(naive_handle, CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(naive_handle, CURLOPT_PROGRESSFUNCTION, progress_callback);
    curl_easy_setopt(naive_handle, CURLOPT_PROGRESSDATA, NULL);
    curl_easy_setopt(naive_handle, CURLOPT_HEADERFUNCTION, header_callback);

    printf("Hello %s\nStart to send requets....\n", name);

    CURLcode success = curl_easy_perform(naive_handle);

    printf("Request send done, success = %d\n", success);

    curl_easy_cleanup(naive_handle);
    curl_global_cleanup();
    return 0;
}
