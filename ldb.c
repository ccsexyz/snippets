#include "util.h"
#include <leveldb/c.h>

typedef struct {
    int fill_cache;
    int verify_checksum;
    int lru_size;
    int max_open_files;
    int sync;
    int bits_per_key;
    long write_buffer_size;
    long max_file_size;

    char *db_name;
    char *compression;
    char *range_start;
    char *range_end;
    char *propname;
} ldb_options_t;

static command_t cmds[] = { { "", "fill_cache", cmd_set_bool, offsetof(ldb_options_t, fill_cache),
                                "on" },
    { "", "verify_checksum", cmd_set_bool, offsetof(ldb_options_t, verify_checksum), "on" },
    { "", "db_name", cmd_set_str, offsetof(ldb_options_t, db_name), "testdb" },
    { "", "lru_size", cmd_set_int, offsetof(ldb_options_t, lru_size), "1024" },
    { "", "max_open_files", cmd_set_int, offsetof(ldb_options_t, max_open_files), "1024" },
    { "", "sync", NULL, offsetof(ldb_options_t, sync), "" },
    { "", "bits_per_key", cmd_set_int, offsetof(ldb_options_t, bits_per_key), "12" },
    { "", "compression", cmd_set_str, offsetof(ldb_options_t, compression), "snappy" },
    { "", "range_start", cmd_set_str, offsetof(ldb_options_t, range_start), "" },
    { "", "range_end", cmd_set_str, offsetof(ldb_options_t, range_end), "" },
    { "", "write_buffer_size", cmd_set_size, offsetof(ldb_options_t, write_buffer_size), "16m" },
    { "", "max_file_size", cmd_set_size, offsetof(ldb_options_t, max_file_size), "16m" },
    { "", "propname", cmd_set_str, offsetof(ldb_options_t, propname), "" } };

static ldb_options_t *
ldb_create_options()
{
    ldb_options_t *opt = (ldb_options_t *)calloc(sizeof(ldb_options_t), 1);
    return opt;
}

static void
ldb_options_destroy(ldb_options_t *opt)
{
    free(opt->db_name);
    free(opt->compression);
    free(opt->range_start);
    free(opt->range_end);
    free(opt->propname);
    free(opt);
}

static leveldb_readoptions_t *
create_readoptions(ldb_options_t *opt)
{
    leveldb_readoptions_t *ropt = leveldb_readoptions_create();

    if (opt->fill_cache) {
        leveldb_readoptions_set_fill_cache(ropt, 1);
    }
    if (opt->verify_checksum) {
        leveldb_readoptions_set_verify_checksums(ropt, 1);
    }

    return ropt;
}

static leveldb_writeoptions_t *
create_writeoptions(ldb_options_t *opt)
{
    leveldb_writeoptions_t *wopt = leveldb_writeoptions_create();

    if (opt->sync) {
        leveldb_writeoptions_set_sync(wopt, 1);
    }

    return wopt;
}

static void
ldb_get(leveldb_t *ldb, const char *db_name, const char *line, leveldb_readoptions_t *ropt)
{
    char *errstr = NULL;
    struct timeval tv = tv_now();
    size_t vallen = 0;

    char *value = leveldb_get(ldb, ropt, line, strlen(line), &vallen, &errstr);

    log_info("leveldb_get key <%s> %s, vallen = %lu, errstr = %s, taken %.2fms", line,
        value ? "HIT" : "MISS", vallen, errstr ? errstr : "errstr is empty",
        tv_sub_msec_double(tv_now(), tv));

    leveldb_free(errstr);
    leveldb_free(value);
}

static void
process_ldb_get(leveldb_t *ldb, ldb_options_t *opt)
{
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

        ldb_get(ldb, opt->db_name, line, ropt);
    }

    leveldb_readoptions_destroy(ropt);
}

static void
ldb_put(leveldb_t *ldb, const char *db_name, const char *line, leveldb_writeoptions_t *wopt)
{
    char *errstr = NULL;
    struct timeval tv = tv_now();

    leveldb_put(ldb, wopt, line, strlen(line), line, strlen(line), &errstr);

    if (errstr) {
        log_info("leveldb_put key <%s> to db %s error: %s", line, db_name, errstr);
        leveldb_free(errstr);
    } else {
        // log_info("leveldb_put key <%s> to db %s taken %.2fms", line, db_name,
        // tv_sub_msec_double(tv_now(), tv));
    }
}

static void
process_ldb_put(leveldb_t *ldb, ldb_options_t *opt)
{
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

static void
process_ldb_keys(leveldb_t *ldb, ldb_options_t *opt)
{
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
            log_info("process key <%.*s> klen = %lu, taken %.2fms", (int)klen, key, klen, ms_taken);
        }

        leveldb_iter_next(iter);
    }

    leveldb_iter_destroy(iter);
    leveldb_readoptions_destroy(ropt);
}

static void
process_ldb_compact_range(leveldb_t *ldb, ldb_options_t *opt)
{
    log_info("start to compact_range start <%s> end <%s>", opt->range_start, opt->range_end);

    struct timeval tv_start = tv_now();
    leveldb_compact_range(
        ldb, opt->range_start, strlen(opt->range_start), opt->range_end, strlen(opt->range_end));
    log_info("compact finished! taken %.2fms", tv_sub_msec_double(tv_now(), tv_start));
}

static void
process_ldb_property(leveldb_t *ldb, ldb_options_t *opt)
{
    char *value = leveldb_property_value(ldb, opt->propname);

    if (value == NULL) {
        log_info("get property <%s> but return NULL", opt->propname);
    } else {
        log_info("%s", value);
        leveldb_free(value);
    }
}

static void
process_ldb_repair(ldb_options_t *ldb_opt, leveldb_options_t *opt)
{
    char *errstr = NULL;

    struct timeval tv_start = tv_now();
    leveldb_repair_db(opt, ldb_opt->db_name, &errstr);

    log_info("repair db <%s> %s! taken %.2fms", ldb_opt->db_name,
        errstr == NULL ? "successful!" : errstr, tv_sub_msec_double(tv_now(), tv_start));
    leveldb_free(errstr);
}

static void
process_ldb(leveldb_t *ldb, const char *typ, const char *errstr, ldb_options_t *opt)
{
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

int
main(int argc, const char *argv[])
{
    const char *base = basename(strdup(argv[0]));

    if (argc < 2) {
        printf("usage: %s get|put|keys|compact_range|property|repair [db_name=db] "
               "[fill_cache=on|off] [verify_checksum=on|off]\n",
            base);
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

    leveldb_options_t *opt = leveldb_options_create();
    leveldb_options_set_create_if_missing(opt, 1);
    leveldb_options_set_max_open_files(opt, ldb_opt->max_open_files);
    leveldb_options_set_write_buffer_size(opt, ldb_opt->write_buffer_size);
    // leveldb_options_set_max_file_size(opt, ldb_opt->max_file_size);
    log_info("write_buffer_size is %ld", ldb_opt->write_buffer_size);
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

    if (!strcasecmp(type, "repair")) {
        process_ldb_repair(ldb_opt, opt);
    } else {
        char *errstr = NULL;
        leveldb_t *ldb = leveldb_open(opt, ldb_opt->db_name, &errstr);

        process_ldb(ldb, type, errstr, ldb_opt);
        leveldb_free(errstr);

        if (ldb) {
            leveldb_close(ldb);
        }
    }

    leveldb_cache_destroy(cache);
    if (filter) {
        leveldb_filterpolicy_destroy(filter);
    }
    leveldb_options_destroy(opt);
    ldb_options_destroy(ldb_opt);
    free(type);

    return 0;
}
