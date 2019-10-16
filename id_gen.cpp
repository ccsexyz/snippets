#include "util.h"

static pthread_once_t global_id_once_1 = PTHREAD_ONCE_INIT;
static uint64_t global_id_pool_1 = 0;

static pthread_once_t global_id_once_2 = PTHREAD_ONCE_INIT;
static uint64_t global_id_pool_2 = 0;
static uint64_t __thread tls_id_start = 0;
static uint64_t __thread tls_id_end = 0;

struct v1 {
public:
    inline uint64_t
    id_gen_impl()
    {
        pthread_once(&global_id_once_1, global_id_init);
        return __sync_fetch_and_add(&global_id_pool_1, 1);
    }

private:
    static void
    global_id_init()
    {
        uint32_t seed = (uint32_t)tv_now_usec();
        global_id_pool_1 = rand_r(&seed);
    }
};

template <int N> struct v2 {
public:
    inline uint64_t
    id_gen_impl()
    {
        pthread_once(&global_id_once_2, global_id_init);

        if (tls_id_start == tls_id_end) {
            tls_id_start = __sync_fetch_and_add(&global_id_pool_2, N);
            tls_id_end = tls_id_start + N;
        }

        return tls_id_start++;
    }

private:
    static void
    global_id_init()
    {
        uint32_t seed = (uint32_t)tv_now_usec();
        global_id_pool_2 = rand_r(&seed);
    }
};

template <typename T>
void
test(int n, int thread_num)
{
    auto thread_func = [&] {
        T t;
        elapsed e(get_filt_type_name<T>());

        for (int i = 0; i < n; i++) {
            t.id_gen_impl();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_num; i++) {
        threads.emplace_back(thread_func);
    }
    for (auto &t : threads) {
        t.join();
    }
}

int
main(void)
{
    int n = 3000000;
    int thread_num = 4;

    test<v1>(n, thread_num);
    test<v2<1>>(n, thread_num);
    test<v2<2>>(n, thread_num);
    test<v2<4>>(n, thread_num);
    test<v2<8>>(n, thread_num);
    test<v2<16>>(n, thread_num);
    test<v2<128>>(n, thread_num);
    test<v2<1024>>(n, thread_num);
    test<v2<4096>>(n, thread_num);
    test<v2<40960>>(n, thread_num);

    return 0;
}

/*
 * Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz
 *
    $ ./id_gen | c++filt
    v1 taken 241.63ms
    v1 taken 241.82ms
    v1 taken 243.44ms
    v1 taken 245.64ms
    v2<1> taken 385.25ms
    v2<1> taken 436.70ms
    v2<1> taken 447.39ms
    v2<1> taken 451.23ms
    v2<2> taken 227.57ms
    v2<2> taken 240.50ms
    v2<2> taken 241.01ms
    v2<2> taken 244.32ms
    v2<4> taken 110.02ms
    v2<4> taken 127.95ms
    v2<4> taken 148.68ms
    v2<4> taken 155.10ms
    v2<8> taken 65.66ms
    v2<8> taken 92.64ms
    v2<8> taken 104.32ms
    v2<8> taken 107.03ms
    v2<16> taken 43.51ms
    v2<16> taken 82.81ms
    v2<16> taken 83.71ms
    v2<16> taken 92.91ms
    v2<128> taken 28.53ms
    v2<128> taken 60.44ms
    v2<128> taken 73.80ms
    v2<128> taken 84.65ms
    v2<1024> taken 30.03ms
    v2<1024> taken 30.06ms
    v2<1024> taken 64.32ms
    v2<1024> taken 72.76ms
    v2<4096> taken 26.55ms
    v2<4096> taken 62.91ms
    v2<4096> taken 80.60ms
    v2<4096> taken 89.76ms
    v2<40960> taken 45.74ms
    v2<40960> taken 55.87ms
    v2<40960> taken 83.25ms
    v2<40960> taken 94.51ms

*/
