#include <iostream>
#include <sys/time.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <typeinfo>

static struct timeval tv_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv;
}

static long tv_sub_msec(struct timeval end, struct timeval start) {
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
}

struct v1 {
public:
    inline int string_printf_impl(std::string& output, const char* format, ...) {
        // Tru to the space at the end of output for our output buffer.
        // Find out write point then inflate its size temporarily to its
        // capacity; we will later shrink it to the size needed to represent
        // the formatted string.  If this buffer isn't large enough, we do a
        // resize and try again.

        va_list args;
        va_start(args, format);

        const int write_point = output.size();
        int remaining = output.capacity() - write_point;
        output.resize(output.capacity());

        va_list copied_args;
        va_copy(copied_args, args);
        int bytes_used = vsnprintf(&output[write_point], remaining, format,
                                   copied_args);
        va_end(copied_args);
        if (bytes_used < 0) {
            return -1;
        } else if (bytes_used < remaining) {
            // There was enough room, just shrink and return.
            output.resize(write_point + bytes_used);
        } else {
            output.resize(write_point + bytes_used + 1);
            remaining = bytes_used + 1;
            bytes_used = vsnprintf(&output[write_point], remaining, format, args);
            if (bytes_used + 1 != remaining) {
                return -1;
            }
            output.resize(write_point + bytes_used);
        }
        return 0;
    }
};

static __thread char s_tls_printf_buf[4096];

struct v2 {
public:
    inline int string_printf_impl(std::string &output, const char *format, ...) {
        va_list args;
        va_start(args, format);

        va_list copied_args;
        va_copy(copied_args, args);
        int bytes_used = vsnprintf(s_tls_printf_buf, sizeof(s_tls_printf_buf), format, copied_args);
        va_end(copied_args);

        if (bytes_used < 0) {
            return -1;
        }

        if (bytes_used >= sizeof(s_tls_printf_buf)) {
            size_t bufsz = bytes_used + 1;
            char *buf = (char *)malloc(bufsz);
            bytes_used = vsnprintf(buf, bufsz, format, args);
            if (bytes_used + 1 != bufsz) {
                free(buf);
                return -1;
            }
            output.append(buf, bytes_used);
            free(buf);
        } else {
            output.append(s_tls_printf_buf, bytes_used);
        }

        return 0;
    }
};

template<typename T>
void test(int n) {
    struct timeval tv = tv_now();
    std::string s;
    T t;

    for (int i = 0; i < n; i++) {
        int rc = t.string_printf_impl(s, "lsjdflsjdlfjsldfjlsdjflsdjflsjdlfkjasljflasjflajslfjaslkdjflasjdfljasldfjlasjdflasjldfjasljflkasjdflkajslfjlasjflajslkdfjalksdjflasjdlfjasldjflasjdlfjasldfjlasjdflasjlkdfjlasdjflasjdlfjsd i is %d\n", i);
        assert(rc == 0);
    }

    printf("%s %s: loop %d times, taken %ld msec, s.length = %ld\n", __func__, typeid(T).name(), n, tv_sub_msec(tv_now(), tv), s.length());
}

int main(int argc, const char **argv) {
    int n = 30000;
    if (argc > 1) {
        n = atoi(argv[1]);
    }

    test<v1>(n);
    test<v2>(n);
}

/*
 * Intel(R) Core(TM) i7-8700 CPU @ 3.20GHz
$ ./string_printf 100000
test 2v1: loop 100000 times, taken 12539 msec, s.length = 19888890
test 2v2: loop 100000 times, taken 25 msec, s.length = 19888890
 *
 */
