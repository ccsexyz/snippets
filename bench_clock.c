#include "util.h"

static void bench_clockgettime(clockid_t id, const char *name) {
    struct timeval tv_start = tv_now();
    struct timespec spec;

    for (int i = 0; i < 10000000; i++) {
        clock_gettime(id, &spec);
    }

    struct timeval tv_end = tv_now();
    double ms_taken = tv_sub_msec_double(tv_end, tv_start);
    printf("bench %s taken %.2f ms, %.2f ns/op\n", name, ms_taken, ms_taken / 10);
}

#define BENCH_CLOCKGETTIME(ID) bench_clockgettime((ID), (#ID))

static void bench_gettimeofday() {
    struct timeval tv_start = tv_now();
    struct timeval tv;

    for (int i = 0; i < 10000000; i++) {
        gettimeofday(&tv, NULL);
    }

    struct timeval tv_end = tv_now();
    double ms_taken = tv_sub_msec_double(tv_end, tv_start);
    printf("bench gettimeofday taken %.2f ms, %.2f ns/op\n", ms_taken, ms_taken / 10);
}

int main(void) {
    BENCH_CLOCKGETTIME(CLOCK_REALTIME);
    BENCH_CLOCKGETTIME(CLOCK_REALTIME_COARSE);
    BENCH_CLOCKGETTIME(CLOCK_MONOTONIC);
    BENCH_CLOCKGETTIME(CLOCK_MONOTONIC_COARSE);
    BENCH_CLOCKGETTIME(CLOCK_MONOTONIC_RAW);
    BENCH_CLOCKGETTIME(CLOCK_BOOTTIME);
    BENCH_CLOCKGETTIME(CLOCK_PROCESS_CPUTIME_ID);
    BENCH_CLOCKGETTIME(CLOCK_THREAD_CPUTIME_ID);

    bench_gettimeofday();

    return 0;
}

/*
 * Intel(R) Core(TM) i7-8700 CPU @ 3.20GHz
$ gcc -O2 -o bench_clock bench_clock.c; ./bench_clock
bench CLOCK_REALTIME taken 144.53 ms, 14.45 ns/op
bench CLOCK_REALTIME_COARSE taken 36.12 ms, 3.61 ns/op
bench CLOCK_MONOTONIC taken 153.77 ms, 15.38 ns/op
bench CLOCK_MONOTONIC_COARSE taken 37.50 ms, 3.75 ns/op
bench CLOCK_MONOTONIC_RAW taken 3342.96 ms, 334.30 ns/op
bench CLOCK_BOOTTIME taken 3353.16 ms, 335.32 ns/op
bench CLOCK_PROCESS_CPUTIME_ID taken 3859.14 ms, 385.91 ns/op
bench CLOCK_THREAD_CPUTIME_ID taken 3821.97 ms, 382.20 ns/op
bench gettimeofday taken 157.60 ms, 15.76 ns/op
*/
