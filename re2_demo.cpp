#include "re2/re2.h"
#include "util.h"

using std::string;

static void
match(const RE2 &re, const std::string &str)
{
    auto rc = RE2::FullMatch(str, re);
    printf("rc = %d\n", rc);
}

int
main(int argc, const char **argv)
{
    const char *base = basename(strdup(argv[0]));

    if (argc < 3) {
        printf("usage: %s <regex-pattern> <str1> ... <strN>\n", base);
        return 1;
    }

    RE2 re(argv[1]);

    for (int i = 2; i < argc; i++) {
        elapsed e("match <" + string(argv[i]) + ">");
        match(re, argv[i]);
    }

    return 0;
}
