#include "util.h"
#include <leveldb/c.h>

static int fill_cache;
static int verify_checksum;
static int lru_size;
static int max_open_files;
static int sync;
static int bits_per_key;

static char *db_name;
static char *compression;

static leveldb_readoptions_t *create_readoptions() {
    leveldb_readoptions_t *ropt = leveldb_readoptions_create();

    if (fill_cache) {
        leveldb_readoptions_set_fill_cache(ropt, 1);
    }
    if (verify_checksum) {
        leveldb_readoptions_set_verify_checksums(ropt, 1);
    }

    return ropt;
}

static leveldb_writeoptions_t *create_writeoptions() {
    leveldb_writeoptions_t *wopt = leveldb_writeoptions_create();

    if (sync) {
        leveldb_writeoptions_set_sync(wopt, 1);
    }

    return wopt;
}

static void ldb_get(leveldb_t *ldb, const char *db_name, const char *line, leveldb_readoptions_t *ropt) {
    char *errstr = NULL;
    struct timeval tv = tv_now();
    size_t vallen = 0;

    char *value = leveldb_get(ldb, ropt, line, strlen(line), &vallen, &errstr);

    printf("leveldb_get key <%s> %s, vallen = %lu, errstr = %s, taken %.2fms\n", line, value ? "HIT" : "MISS", vallen, errstr ? errstr : "errstr is empty", tv_sub_msec_double(tv_now(), tv));

    leveldb_free(errstr);
    leveldb_free(value);
}

static void process_ldb_get(leveldb_t *ldb) {
    const char *line = NULL;
    char buf[1024 * 16];

    leveldb_readoptions_t *ropt = create_readoptions();

    while (line = fgets(buf, sizeof(buf), stdin)) {
        char *enter = strstr(line, "\n");
        if (enter) {
            if (enter == line) {
                continue;
            }
            *enter = '\0';
        }

        for (int i = 0; i < 1000; i++) {
            ldb_get(ldb, db_name, line, ropt);
        }
    }

    leveldb_readoptions_destroy(ropt);
}

static void ldb_put(leveldb_t *ldb, const char *db_name, const char *line, leveldb_writeoptions_t *wopt) {
    char *errstr = NULL;
    struct timeval tv = tv_now();

    leveldb_put(ldb, wopt, line, strlen(line), line, strlen(line), &errstr);

    if (errstr) {
        printf("leveldb_put key <%s> to db %s error: %s\n", line, db_name, errstr);
        leveldb_free(errstr);
    } else {
        printf("leveldb_put key <%s> to db %s taken %.2fms\n", line, db_name, tv_sub_msec_double(tv_now(), tv));
    }
}

static void process_ldb_put(leveldb_t *ldb) {
    const char *line = NULL;
    char buf[1024 * 16];

    leveldb_writeoptions_t *wopt = create_writeoptions();

    while (line = fgets(buf, sizeof(buf), stdin)) {
        char *enter = strstr(line, "\n");
        if (enter) {
            if (enter == line) {
                continue;
            }
            *enter = '\0';
        }

        ldb_put(ldb, db_name, line, wopt);
    }

    leveldb_writeoptions_destroy(wopt);
}

static void process_ldb_keys(leveldb_t *ldb) {
    leveldb_readoptions_t *ropt = create_readoptions();
    leveldb_iterator_t *iter = leveldb_create_iterator(ldb, ropt);

    struct timeval tv = tv_now();
    leveldb_iter_seek_to_first(iter);
    while (leveldb_iter_valid(iter)) {
        size_t klen = 0;
        const char *key = leveldb_iter_key(iter, &klen);
        struct timeval tv_end = tv_now();
        double ms_taken = tv_sub_msec_double(tv_end, tv);
        tv = tv_end;

        if (key) {
            printf("process key <%.*s> klen = %lu, taken %.2fms\n", (int)klen, key, klen, ms_taken);
        }

        leveldb_iter_next(iter);
    }

    leveldb_iter_destroy(iter);
    leveldb_readoptions_destroy(ropt);
}

static void process_ldb(leveldb_t *ldb, const char *typ, const char *errstr) {
    if (ldb == NULL) {
        if (errstr == NULL) {
            errstr = "no error str";
        }
        printf("open %s error: %s\n", db_name, errstr);
        return;
    }

    if (typ == NULL) {
        printf("type is NULL, impossible!\n");
    } else if (!strcasecmp(typ, "get")) {
        process_ldb_get(ldb);
    } else if (!strcasecmp(typ, "put")) {
        process_ldb_put(ldb);
    } else if (!strcasecmp(typ, "keys")) {
        process_ldb_keys(ldb);
    } else {
        printf("invalid type %s\n", typ);
    }
}

static void init() {
    db_name = "testdb";
    compression = "";
    lru_size = 1024;
    max_open_files = 1024;
    bits_per_key = 12;
}

static void init_kv(key_value_t *kv) {
    for (key_value_t *p = kv; p; p = p->next) {
        int onoff = !strcasecmp(p->value, "on");
        int num = atoi(p->value);

        if (!strcasecmp(p->key, "fill_cache")) {
            fill_cache = onoff;
        } else if (!strcasecmp(p->key, "verify_checksum")) {
            verify_checksum = onoff;
        } else if (!strcasecmp(p->key, "db_name")) {
            db_name = strdup(p->value);
        } else if (!strcasecmp(p->key, "lru_size")) {
            lru_size = num;
        } else if (!strcasecmp(p->key, "compression")) {
            compression = strdup(p->value);
        } else if (!strcasecmp(p->key, "max_open_files")) {
            max_open_files = num;
        } else if (!strcasecmp(p->key, "sync")) {
            sync = onoff;
        }
    }
}

int main(int argc, const char *argv[]) {
    const char *base = basename(strdup(argv[0]));

    if (argc < 2) {
        printf("usage: %s get|put|keys [db_name=db] [fill_cache=on|off] [verify_checksum=on|off]\n", base);
        return 1;
    }

    init();

    key_value_t *kv = parse_key_values_from_str_array(&argv[2], argc - 2);
    init_kv(kv);

    leveldb_options_t *opt = leveldb_options_create();
    leveldb_options_set_create_if_missing(opt, 1);
    leveldb_options_set_max_open_files(opt, max_open_files);
    if (!strcasecmp(compression, "snappy")) {
        leveldb_options_set_compression(opt, leveldb_snappy_compression);
    }
    leveldb_filterpolicy_t *filter = NULL;
    if (bits_per_key > 0) {
        filter = leveldb_filterpolicy_create_bloom(bits_per_key);
        leveldb_options_set_filter_policy(opt, filter);
    }


    leveldb_cache_t *cache = leveldb_cache_create_lru(lru_size);
    if (lru_size > 0) {
        leveldb_options_set_cache(opt, cache);
    }
    
    // leveldb_options_set_max_file_size(opt, 1024 * 1024 * 8);

    char *errstr = NULL;
    leveldb_t *ldb = leveldb_open(opt, db_name, &errstr);

    process_ldb(ldb, argv[1], errstr);
    leveldb_free(errstr);
    
    leveldb_close(ldb);
    leveldb_cache_destroy(cache);
    leveldb_options_destroy(opt);
    free_all_key_values(kv);

    return 0;

}
