#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <libgen.h>

static double tv_sub_msec_double(struct timeval end, struct timeval start) {
    return ((double)(end.tv_sec - start.tv_sec)) * 1000 + ((
        double)(end.tv_usec - start.tv_usec)) / 1000;
}

static struct timeval tv_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv;
}

#ifdef __cplusplus

#include <iostream>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>

using namespace std;

class elapsed {
public:
    elapsed(const string &info) : tv_(tv_now()), info_(info) {}
    ~elapsed() { printf("%s taken %.2fms\n", info_.data(), tv_sub_msec_double(tv_now(), tv_)); }

private:
    struct timeval tv_;
    string info_;
};

#endif
