#include "util.h"
#include <spawn.h>

typedef struct {
    size_t memory_used;
} config;

static config g_cfg;

static command_t cmds[] = {
    {
        "m",
        "memory_used",
        cmd_set_size,
        offsetof(config, memory_used),
        "400M"
    }
};

static void alloc_memory_from_system() {
    char *p = malloc(g_cfg.memory_used);
    memset(p, 1, g_cfg.memory_used);
}

static void do_fork_test() {
    struct timeval tv_begin = tv_now();
    pid_t pid = fork();
    double ms_taken = tv_sub_msec_double(tv_now(), tv_begin);


    if (pid < 0) {
        log_fatal("fork error: %s", strerror(errno));
    }

    if (pid == 0) {
        _Exit(0);
    }

    log_info("fork taken %.02fms", ms_taken);

    int retval = -1;
    waitpid(pid, &retval, 0);
    log_info("child process return %d", retval);
}

static void do_posix_spawn_test() {
    pid_t pid;
    const char *argv[] = {"ls", "-al", NULL};

    struct timeval tv_begin = tv_now();
    posix_spawn(&pid, "/bin/ls", NULL, NULL, argv, NULL);
    double ms_taken = tv_sub_msec_double(tv_now(), tv_begin);

    log_info("pid is %d, taken %.02fms", pid, ms_taken);
}

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void *lock_cycle(void *arg_not_used) {
    pthread_mutex_lock(&g_lock);
    log_info("lock!");
    sleep(5);
    pthread_mutex_unlock(&g_lock);
    log_info("unlock!");
    return NULL;
}

static void do_deadlock_test() {
    pthread_t tid;
    int rc = pthread_create(&tid, NULL, lock_cycle, NULL);
    assert(rc == 0);

    sleep(1);

    pid_t pid = vfork();
    if (pid < 0) {
        log_fatal("fork error: %s", strerror(errno));
    }

    if (pid == 0) {
        log_info("start to lock in child process!");
        pthread_mutex_lock(&g_lock);
        log_info("lock success!");
        pthread_mutex_unlock(&g_lock);
        _Exit(0);
    } else {
        waitpid(pid, NULL, 0);
    }

    pthread_join(tid, NULL);
}

int main(int argc, char **argv) {
    char *errstr = NULL;
    int rc = parse_command_args(argc, argv, &g_cfg, cmds, array_size(cmds), &errstr, NULL);
    if (rc != 0) {
        log_fatal("parse command error: %s", errstr ? errstr : "");
    }
    free(errstr);

    alloc_memory_from_system();

    do_fork_test();
    do_posix_spawn_test();
    do_deadlock_test();

    return 0;
}
