#include "util.h"
#include <rocksdb/c.h>

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

static command_t cmds[] = {
    {
        "",
        "fill_cache",
        cmd_set_bool,
        offsetof(ldb_options_t, fill_cache),
        "on"
    },
    {
        "",
        "verify_checksum",
        cmd_set_bool,
        offsetof(ldb_options_t, verify_checksum),
        "on"
    },
    {
        "",
        "db_name",
        cmd_set_str,
        offsetof(ldb_options_t, db_name),
        "testdb"
    },
    {
        "",
        "lru_size",
        cmd_set_int,
        offsetof(ldb_options_t, lru_size),
        "1024"
    },
    {
        "",
        "max_open_files",
        cmd_set_int,
        offsetof(ldb_options_t, max_open_files),
        "1024"
    },
    {
        "",
        "sync",
        NULL,
        offsetof(ldb_options_t, sync),
        ""
    },
    {
        "",
        "bits_per_key",
        cmd_set_int,
        offsetof(ldb_options_t, bits_per_key),
        "12"
    },
    {
        "",
        "compression",
        cmd_set_str,
        offsetof(ldb_options_t, compression),
        "snappy"
    },
    {
        "",
        "range_start",
        cmd_set_str,
        offsetof(ldb_options_t, range_start),
        ""
    },
    {
        "",
        "range_end",
        cmd_set_str,
        offsetof(ldb_options_t, range_end),
        ""
    },
    {
        "",
        "propname",
        cmd_set_str,
        offsetof(ldb_options_t, propname),
        ""
    }
};

static ldb_options_t *ldb_create_options() {
    ldb_options_t *opt = (ldb_options_t *)calloc(sizeof(ldb_options_t), 1);
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

static rocksdb_readoptions_t *create_readoptions(ldb_options_t *opt) {
    rocksdb_readoptions_t *ropt = rocksdb_readoptions_create();

    if (opt->fill_cache) {
        rocksdb_readoptions_set_fill_cache(ropt, 1);
    }
    if (opt->verify_checksum) {
        rocksdb_readoptions_set_verify_checksums(ropt, 1);
    }

    return ropt;
}

static rocksdb_writeoptions_t *create_writeoptions(ldb_options_t *opt) {
    rocksdb_writeoptions_t *wopt = rocksdb_writeoptions_create();

    if (opt->sync) {
        rocksdb_writeoptions_set_sync(wopt, 1);
    }

    return wopt;
}

static void ldb_get(rocksdb_t *ldb, const char *db_name, const char *line, rocksdb_readoptions_t *ropt) {
    char *errstr = NULL;
    struct timeval tv = tv_now();
    size_t vallen = 0;

    char *value = rocksdb_get(ldb, ropt, line, strlen(line), &vallen, &errstr);

    log_info("rocksdb_get key <%s> %s, vallen = %lu, errstr = %s, taken %.2fms", line, value ? "HIT" : "MISS", vallen, errstr ? errstr : "errstr is empty", tv_sub_msec_double(tv_now(), tv));

    rocksdb_free(errstr);
    rocksdb_free(value);
}

static void process_ldb_get(rocksdb_t *ldb, ldb_options_t *opt) {
    const char *line = NULL;
    char buf[1024 * 16];

    rocksdb_readoptions_t *ropt = create_readoptions(opt);

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

    rocksdb_readoptions_destroy(ropt);
}

static void ldb_put(rocksdb_t *ldb, const char *db_name, const char *line, rocksdb_writeoptions_t *wopt) {
    char *errstr = NULL;
    struct timeval tv = tv_now();
    static int i = 0;

    rocksdb_put(ldb, wopt, line, strlen(line), line, strlen(line), &errstr);

    if (errstr) {
        log_info("rocksdb_put key <%s> to db %s error: %s", line, db_name, errstr);
        rocksdb_free(errstr);
    } else {
        i++;
        if (i % 10000 == 0) {
            log_info("rocksdb_put key <%s> to db %s taken %.2fms", line, db_name, tv_sub_msec_double(tv_now(), tv));
        }
    }
}

static void process_ldb_put(rocksdb_t *ldb, ldb_options_t *opt) {
    const char *line = NULL;
    char buf[1024 * 16];

    rocksdb_writeoptions_t *wopt = create_writeoptions(opt);

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

    rocksdb_writeoptions_destroy(wopt);
}

static void process_ldb_keys(rocksdb_t *ldb, ldb_options_t *opt) {
    rocksdb_readoptions_t *ropt = create_readoptions(opt);
    rocksdb_iterator_t *iter = rocksdb_create_iterator(ldb, ropt);

    struct timeval tv = tv_now();
    rocksdb_iter_seek_to_first(iter);
    while (rocksdb_iter_valid(iter)) {
        size_t klen = 0;
        size_t vlen = 0;
        const char *key = rocksdb_iter_key(iter, &klen);
        const char *value = rocksdb_iter_value(iter, &vlen);
        struct timeval tv_end = tv_now();
        double ms_taken = tv_sub_msec_double(tv_end, tv);
        tv = tv_end;

        if (key) {
            log_info("process key <%.*s> klen = %lu, value <%.*s> vlen = %lu, taken %.2fms", (int)klen, key, klen, (int)vlen, value, vlen, ms_taken);
        }

        rocksdb_iter_next(iter);
    }

    rocksdb_iter_destroy(iter);
    rocksdb_readoptions_destroy(ropt);
}

static void process_ldb_compact_range(rocksdb_t *ldb, ldb_options_t *opt) {
    log_info("start to compact_range start <%s> end <%s>", opt->range_start, opt->range_end);

    struct timeval tv_start = tv_now();
    rocksdb_compact_range(ldb, opt->range_start, strlen(opt->range_start), opt->range_end, strlen(opt->range_end));
    log_info("compact finished! taken %.2fms", tv_sub_msec_double(tv_now(), tv_start));
}

static void process_ldb_property(rocksdb_t *ldb, ldb_options_t *opt) {
    char *value = rocksdb_property_value(ldb, opt->propname);

    if (value == NULL) {
        log_info("get property <%s> but return NULL", opt->propname);
    } else {
        log_info("%s", value);
        rocksdb_free(value);
    }
}

static void process_ldb_repair(ldb_options_t *ldb_opt, rocksdb_options_t *opt) {
    char *errstr = NULL;

    struct timeval tv_start = tv_now();
    rocksdb_repair_db(opt, ldb_opt->db_name, &errstr);

    log_info("repair db <%s> %s! taken %.2fms", ldb_opt->db_name, errstr == NULL ? "successful!" : errstr, tv_sub_msec_double(tv_now(), tv_start));
    rocksdb_free(errstr);
}

static void process_ldb(rocksdb_t *ldb, const char *typ, const char *errstr, ldb_options_t *opt) {
    if (ldb == NULL) {
        if (errstr == NULL) {
            errstr = "no error str";
        }
        log_info("open %s error: %s", opt->db_name, errstr);
        return;
    }

    if (typ == NULL) {
        log_info("type is NULL, impossible!");
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
        log_info("invalid type %s", typ);
    }
}

int main(int argc, const char *argv[]) {
    const char *base = basename(strdup(argv[0]));

    if (argc < 2) {
        printf("usage: %s get|put|keys|compact_range|property|repair [db_name=db] [fill_cache=on|off] [verify_checksum=on|off]\n", base);
        return 1;
    }

    char *errstr = NULL;
    char *type = NULL;
    ldb_options_t *ldb_opt = ldb_create_options();
    int rc = parse_command_args(argc, argv, ldb_opt, cmds, array_size(cmds), &errstr, &type);
    if (rc != 0) {
        log_fatal("parse command error: %s", errstr ? errstr : "");
    }
    if (str_empty(type)) {
        log_fatal("no valid type");
    }
    free(errstr);

    rocksdb_options_t *opt = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(opt, 1);
    rocksdb_options_set_max_open_files(opt, ldb_opt->max_open_files);
    //rocksdb_options_set_target_file_size_base(opt, 32 * 1024 * 1024);
    //rocksdb_options_set_target_file_size_multiplier(opt, 2);
    if (!strcasecmp(ldb_opt->compression, "snappy")) {
        rocksdb_options_set_compression(opt, rocksdb_snappy_compression);
    }
    rocksdb_filterpolicy_t *filter = NULL;
    if (ldb_opt->bits_per_key > 0) {
        filter = rocksdb_filterpolicy_create_bloom(ldb_opt->bits_per_key);
        //rocksdb_options_set_filter_policy(opt, filter);
    }


    rocksdb_cache_t *cache = rocksdb_cache_create_lru(ldb_opt->lru_size);
    if (ldb_opt->lru_size > 0) {
        //rocksdb_options_set_cache(opt, cache);
    }
    
    // rocksdb_options_set_max_file_size(opt, 1024 * 1024 * 8);

    if (!strcasecmp(type, "repair")) {
        process_ldb_repair(ldb_opt, opt);
    } else {
        char *errstr = NULL;
        rocksdb_t *ldb = rocksdb_open(opt, ldb_opt->db_name, &errstr);

        process_ldb(ldb, type, errstr, ldb_opt);
        rocksdb_free(errstr);

        if (ldb) {
            rocksdb_close(ldb);
        }
    }

    rocksdb_cache_destroy(cache);
    if (filter) {
        rocksdb_filterpolicy_destroy(filter);
    }
    rocksdb_options_destroy(opt);
    ldb_options_destroy(ldb_opt);
    free(type);

    return 0;

}
