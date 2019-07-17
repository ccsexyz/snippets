#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

static size_t write_cb(char *data, size_t n, size_t l, void *userp)
{
    /* take care of the data here, ignored in this example */ 
    (void)data;
    (void)userp;
    return n*l;
}

static void add_transfer(CURLM *cm, const char *url)
{
    CURL *eh = curl_easy_init();
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(eh, CURLOPT_URL, url);
    curl_easy_setopt(eh, CURLOPT_PRIVATE, url);
    curl_multi_add_handle(cm, eh);
}

int main(int argc, char **argv)
{
    CURLM *cm;
    CURLMsg *msg;
    unsigned int transfers = 0;
    int msgs_left = -1;
    int still_alive = 1;

    curl_global_init(CURL_GLOBAL_ALL);
    cm = curl_multi_init();

    for(transfers = 1; transfers < argc; transfers++)
    add_transfer(cm, (const char *)argv[transfers]);

    do {
        curl_multi_perform(cm, &still_alive);

        while((msg = curl_multi_info_read(cm, &msgs_left))) {
            if(msg->msg == CURLMSG_DONE) {
                char *url;
                CURL *e = msg->easy_handle;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &url);
                fprintf(stderr, "R: %d - %s <%s>\n",
                        msg->data.result, curl_easy_strerror(msg->data.result), url);
                curl_multi_remove_handle(cm, e);
                curl_easy_cleanup(e);
            }
            else {
                fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
            }
        }
        if(still_alive)
        curl_multi_wait(cm, NULL, 0, 1000, NULL);

    } while(still_alive);

    curl_multi_cleanup(cm);
    curl_global_cleanup();

    return EXIT_SUCCESS;
}
