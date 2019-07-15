#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <libgen.h>

static double tv_sub_msec_double(struct timeval end, struct timeval start) {
    return ((double)(end.tv_sec - start.tv_sec)) * 1000 + ((
        double)(end.tv_usec - start.tv_usec)) / 1000;
}

static struct timeval tv_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv;
}

typedef struct key_value_s key_value_t;

struct key_value_s {
    char *value;
    key_value_t *next;
    char key[0];
};

static key_value_t *parse_key_value(const char *str) {
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    if (len < 3) {
        return NULL;
    }

    char *equal = strstr(str, "=");
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

static void free_all_key_values(key_value_t *kv) {
    while (kv) {
        key_value_t *kv_to_free = kv;
        kv = kv->next;
        free(kv_to_free);
    }
}

static key_value_t *parse_key_values_from_str_array(const char **str_arr, size_t arr_num) {
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

#ifdef __cplusplus

#include <iostream>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>

using namespace std;

class elapsed {
public:
    elapsed(const string &info) : tv_(tv_now()), info_(info) {}
    ~elapsed() { printf("%s taken %.2fms\n", info_.data(), tv_sub_msec_double(tv_now(), tv_)); }

private:
    struct timeval tv_;
    string info_;
};

#endif
