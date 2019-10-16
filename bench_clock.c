#include "util.h"

static void
bench_clockgettime(clockid_t id, const char *name)
{
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

static void
bench_gettimeofday()
{
    struct timeval tv_start = tv_now();
    struct timeval tv;

    for (int i = 0; i < 10000000; i++) {
        gettimeofday(&tv, NULL);
    }

    struct timeval tv_end = tv_now();
    double ms_taken = tv_sub_msec_double(tv_end, tv_start);
    printf("bench gettimeofday taken %.2f ms, %.2f ns/op\n", ms_taken, ms_taken / 10);
}

static void
bench_coarse_diff()
{
    const int N = 100000;
    double total_msec_double = 0;

    for (int i = 0; i < N; i++) {
        struct timespec spec;
        struct timespec spec_coarse;

        clock_gettime(CLOCK_REALTIME, &spec);
        clock_gettime(CLOCK_REALTIME_COARSE, &spec_coarse);

        double msec_double = ts_sub_msec_double(spec_coarse, spec);
        if (msec_double < 0) {
            msec_double = -msec_double;
        }

        total_msec_double += msec_double;
    }

    printf("%s = %.3g ms\n", __func__, total_msec_double / N);
}

int
main(void)
{
    bench_coarse_diff();

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
$ gcc -g -O2 -o bench_clock bench_clock.c -std=c99; ./bench_clock
bench_coarse_diff = 2.79 ms
bench CLOCK_REALTIME taken 150.66 ms, 15.07 ns/op
bench CLOCK_REALTIME_COARSE taken 38.59 ms, 3.86 ns/op
bench CLOCK_MONOTONIC taken 147.96 ms, 14.80 ns/op
bench CLOCK_MONOTONIC_COARSE taken 38.64 ms, 3.86 ns/op
bench CLOCK_MONOTONIC_RAW taken 2350.82 ms, 235.08 ns/op
bench CLOCK_BOOTTIME taken 2361.18 ms, 236.12 ns/op
bench CLOCK_PROCESS_CPUTIME_ID taken 2811.25 ms, 281.13 ns/op
bench CLOCK_THREAD_CPUTIME_ID taken 2781.36 ms, 278.14 ns/op
bench gettimeofday taken 157.63 ms, 15.76 ns/op
 */
