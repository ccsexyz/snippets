#include "util.h"
#include <pcre.h>

static void
match(pcre *compiled, pcre_extra *extra, const char *str)
{
    int rc = pcre_exec(compiled, extra, str, strlen(str), 0, 0, NULL, 0);
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

    const char *pattern = argv[1];
    const char **strs = &argv[2];
    const int nstrs = argc - 2;

    const char *errstr = NULL;
    int erroff = 0;
    pcre *compiled = pcre_compile(pattern, 0, &errstr, &erroff, NULL);

    if (compiled == NULL) {
        printf("compile <%s> failed: %s", pattern, errstr);
        return 1;
    }

    pcre_extra *extra = pcre_study(compiled, 0, &errstr);
    if (errstr != NULL) {
        printf("study <%s> error: %s\n", pattern, errstr);
        return 1;
    }

    printf("start to match <%s>\n", pattern);

    for (int i = 0; i < nstrs; i++) {
        const char *str = strs[i];
        struct timeval tv_start = tv_now();
        match(compiled, extra, str);
        double ms_taken = tv_sub_msec_double(tv_now(), tv_start);
        printf("match <%s> taken %.2fms\n", str, ms_taken);
    }

    return 0;
}