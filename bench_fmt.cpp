#include "util.h"
#include <fmt/format.h>

struct v_snprintf {
public:
    inline std::string impl(uint64_t n) {
        std::string ret;
        for (uint64_t i = 0; i < n; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%llu", i);
            ret += buf;
        }
        return ret;
    }
};

struct v_fmt {
public:
    inline std::string impl(uint64_t n) {
        std::string ret;
        fmt::memory_buffer out;
        for (uint64_t i = 0; i < n; i++) {
            format_to(out, "{}", i);
        }
        ret += out.data();
        return ret;
    }
};

template <typename T>
void bench(uint64_t n) {
    T t;
    elapsed e(get_filt_type_name<T>());
    t.impl(n);
}

int main(int argc, char **argv) {
    bench<v_snprintf>(1000000);
    bench<v_fmt>(1000000);
    return 0;
}
