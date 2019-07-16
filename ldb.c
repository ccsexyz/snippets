#include "util.h"
#include <leveldb/c.h>

typedef struct {
    int fill_cache;
    int verify_checksum;
    int lru_size;
    int max_open_files;
    int sync;
    int bits_per_key;

    char *db_name;
    char *compression;
    char *range_start;
    char *range_end;
    char *propname;
} ldb_options_t;

static ldb_options_t *ldb_create_options() {
    ldb_options_t *opt = (ldb_options_t *)malloc(sizeof(ldb_options_t));

    opt->fill_cache = 1;
    opt->verify_checksum = 1;
    opt->lru_size = 1024;
    opt->max_open_files = 1024;
    opt->sync = 0;
    opt->bits_per_key = 12;

    opt->db_name = strdup("testdb");
    opt->compression = strdup("snappy");
    opt->range_start = strdup("");
    opt->range_end = strdup("");
    opt->propname = strdup("");

    return opt;
}

static void ldb_options_destroy(ldb_options_t *opt) {
    free(opt->db_name);
    free(opt->compression);
    free(opt->range_start);
    free(opt->range_end);
    free(opt->propname);
    free(opt);
}

static void reset_str_ptr(char **pstr, char *new) {
    free(*pstr);
    *pstr = strdup(new);
}

static void ldb_options_parse(ldb_options_t *opt, key_value_t *kv) {
    for (key_value_t *p = kv; p; p = p->next) {
        int onoff = !strcasecmp(p->value, "on");
        int num = atoi(p->value);

        if (!strcasecmp(p->key, "fill_cache")) {
            opt->fill_cache = onoff;
        } else if (!strcasecmp(p->key, "verify_checksum")) {
            opt->verify_checksum = onoff;
        } else if (!strcasecmp(p->key, "db_name")) {
            reset_str_ptr(&opt->db_name, p->value);
        } else if (!strcasecmp(p->key, "lru_size")) {
            opt->lru_size = num;
        } else if (!strcasecmp(p->key, "compression")) {
            reset_str_ptr(&opt->compression, p->value);
        } else if (!strcasecmp(p->key, "max_open_files")) {
            opt->max_open_files = num;
        } else if (!strcasecmp(p->key, "sync")) {
            opt->sync = onoff;
        } else if (!strcasecmp(p->key, "range_start")) {
            reset_str_ptr(&opt->range_start, p->value);
        } else if (!strcasecmp(p->key, "range_end")) {
            reset_str_ptr(&opt->range_end, p->value);
        } else if (!strcasecmp(p->key, "propname")) {
            reset_str_ptr(&opt->propname, p->value);
        } else {
            printf("unexpected key <%s>\n", p->key);
        }
    }
}

static leveldb_readoptions_t *create_readoptions(ldb_options_t *opt) {
    leveldb_readoptions_t *ropt = leveldb_readoptions_create();

    if (opt->fill_cache) {
        leveldb_readoptions_set_fill_cache(ropt, 1);
    }
    if (opt->verify_checksum) {
        leveldb_readoptions_set_verify_checksums(ropt, 1);
    }

    return ropt;
}

static leveldb_writeoptions_t *create_writeoptions(ldb_options_t *opt) {
    leveldb_writeoptions_t *wopt = leveldb_writeoptions_create();

    if (opt->sync) {
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

static void process_ldb_get(leveldb_t *ldb, ldb_options_t *opt) {
    const char *line = NULL;
    char buf[1024 * 16];

    leveldb_readoptions_t *ropt = create_readoptions(opt);

    while ((line = fgets(buf, sizeof(buf), stdin))) {
        char *enter = strstr(line, "\n");
        if (enter) {
            if (enter == line) {
                continue;
            }
            *enter = '\0';
        }

        for (int i = 0; i < 1000; i++) {
            ldb_get(ldb, opt->db_name, line, ropt);
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

static void process_ldb_put(leveldb_t *ldb, ldb_options_t *opt) {
    const char *line = NULL;
    char buf[1024 * 16];

    leveldb_writeoptions_t *wopt = create_writeoptions(opt);

    while ((line = fgets(buf, sizeof(buf), stdin))) {
        char *enter = strstr(line, "\n");
        if (enter) {
            if (enter == line) {
                continue;
            }
            *enter = '\0';
        }

        ldb_put(ldb, opt->db_name, line, wopt);
    }

    leveldb_writeoptions_destroy(wopt);
}

static void process_ldb_keys(leveldb_t *ldb, ldb_options_t *opt) {
    leveldb_readoptions_t *ropt = create_readoptions(opt);
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

static void process_ldb_compact_range(leveldb_t *ldb, ldb_options_t *opt) {
    printf("start to compact_range start <%s> end <%s>\n", opt->range_start, opt->range_end);

    struct timeval tv_start = tv_now();
    leveldb_compact_range(ldb, opt->range_start, strlen(opt->range_start), opt->range_end, strlen(opt->range_end));
    printf("compact finished! taken %.2fms\n", tv_sub_msec_double(tv_now(), tv_start));
}

static void process_ldb_property(leveldb_t *ldb, ldb_options_t *opt) {
    char *value = leveldb_property_value(ldb, opt->propname);

    if (value == NULL) {
        printf("get property <%s> but return NULL\n", opt->propname);
    } else {
        printf("%s\n", value);
        leveldb_free(value);
    }
}

static void process_ldb_repair(ldb_options_t *ldb_opt, leveldb_options_t *opt) {
    char *errstr = NULL;

    struct timeval tv_start = tv_now();
    leveldb_repair_db(opt, ldb_opt->db_name, &errstr);

    printf("repair db <%s> %s! taken %.2fms", ldb_opt->db_name, errstr == NULL ? "successful!" : errstr, tv_sub_msec_double(tv_now(), tv_start));
    leveldb_free(errstr);
}

static void process_ldb(leveldb_t *ldb, const char *typ, const char *errstr, ldb_options_t *opt) {
    if (ldb == NULL) {
        if (errstr == NULL) {
            errstr = "no error str";
        }
        printf("open %s error: %s\n", opt->db_name, errstr);
        return;
    }

    if (typ == NULL) {
        printf("type is NULL, impossible!\n");
    } else if (!strcasecmp(typ, "get")) {
        process_ldb_get(ldb, opt);
    } else if (!strcasecmp(typ, "put")) {
        process_ldb_put(ldb, opt);
    } else if (!strcasecmp(typ, "keys")) {
        process_ldb_keys(ldb, opt);
    } else if (!strcasecmp(typ, "compact_range")) {
        process_ldb_compact_range(ldb, opt);
    } else if (!strcasecmp(typ, "property")) {
        process_ldb_property(ldb, opt);
    } else {
        printf("invalid type %s\n", typ);
    }
}

int main(int argc, const char *argv[]) {
    const char *base = basename(strdup(argv[0]));

    if (argc < 2) {
        printf("usage: %s get|put|keys|compact_range|property|repair [db_name=db] [fill_cache=on|off] [verify_checksum=on|off]\n", base);
        return 1;
    }

    key_value_t *kv = parse_key_values_from_str_array(&argv[2], argc - 2);
    ldb_options_t *ldb_opt = ldb_create_options();
    ldb_options_parse(ldb_opt, kv);

    leveldb_options_t *opt = leveldb_options_create();
    leveldb_options_set_create_if_missing(opt, 1);
    leveldb_options_set_max_open_files(opt, ldb_opt->max_open_files);
    if (!strcasecmp(ldb_opt->compression, "snappy")) {
        leveldb_options_set_compression(opt, leveldb_snappy_compression);
    }
    leveldb_filterpolicy_t *filter = NULL;
    if (ldb_opt->bits_per_key > 0) {
        filter = leveldb_filterpolicy_create_bloom(ldb_opt->bits_per_key);
        leveldb_options_set_filter_policy(opt, filter);
    }


    leveldb_cache_t *cache = leveldb_cache_create_lru(ldb_opt->lru_size);
    if (ldb_opt->lru_size > 0) {
        leveldb_options_set_cache(opt, cache);
    }
    
    // leveldb_options_set_max_file_size(opt, 1024 * 1024 * 8);

    if (!strcasecmp(argv[1], "repair")) {
        process_ldb_repair(ldb_opt, opt);
    } else {
        char *errstr = NULL;
        leveldb_t *ldb = leveldb_open(opt, ldb_opt->db_name, &errstr);

        process_ldb(ldb, argv[1], errstr, ldb_opt);
        leveldb_free(errstr);

        leveldb_close(ldb);
    }

    leveldb_cache_destroy(cache);
    if (filter) {
        leveldb_filterpolicy_destroy(filter);
    }
    leveldb_options_destroy(opt);
    free_all_key_values(kv);
    ldb_options_destroy(ldb_opt);

    return 0;

}
