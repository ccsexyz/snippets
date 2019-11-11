#include "util.h"

int
main(void)
{
    printf("pagesize = %d\n", getpagesize());

    {
        size_t stacksize;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_getstacksize(&attr, &stacksize);
        printf("pthread stack size = %zu bytes \n", stacksize);
    }

    return 0;
}
