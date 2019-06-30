#include <stdio.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <libgen.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>

static ssize_t get_real_size(const char *filename) {
    struct stat file_stat;
    if (stat(filename, &file_stat) < 0) {
        printf("can't stat %s: %s\n", filename, strerror(errno));
        return -1;
    }

    if (S_ISREG(file_stat.st_mode)) {
        return file_stat.st_size;
    } else if (S_ISBLK(file_stat.st_mode)) {
        int fd = open(filename, O_RDWR);
        if (fd < 0) {
            printf("open %s error: %s\n", filename, strerror(errno));
            return -1;
        }

        size_t path_size = 0;
        int rc = ioctl(fd, BLKGETSIZE, &path_size);
        int save_errno = errno;

        close(fd);

        if (rc < 0) {
            printf("ioctl %s error: %s\n", filename, strerror(save_errno));
        }

        return rc < 0 ? -1 : path_size << 9;
    }

    return -1;
}

static double tv_sub_msec_double(struct timeval end, struct timeval start) {
    return ((double)(end.tv_sec - start.tv_sec)) * 1000 + ((
        double)(end.tv_usec - start.tv_usec)) / 1000;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: %s filename destination\n", basename(argv[0]));
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    const char *filename = argv[1];
    char *destination = strdup(argv[2]);

    ssize_t file_size = get_real_size(filename);

    if (file_size < 0) {
        printf("failed to get real size!\n");
        return -1;
    } else if (file_size == 0) {
        printf("file is empty, ignore!\n");
        return 0;
    }

    int fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("open %s error: %s\n", filename, strerror(errno));
        return 1;
    }

    void *addr = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        printf("mmap error: %s\n", strerror(errno));
        return -1;
    }

    char *colon = strstr(destination, ":");
    if (!colon) {
        printf("no colon in address %s\n", destination);
        return -1;
    }

    if (strlen(colon + 1) == 0) {
        printf("no port in address %s\n", destination);
        return -1;
    }

    *colon = '\0';
    const char *host = strdup(destination);
    const char *port = strdup(colon + 1);
    *colon = ':';

    struct addrinfo hints, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &p);
    if (rc) {
        printf("getaddrinfo error: %s\n", gai_strerror(rc));
        return -1;
    }

    int sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock < 0) {
        printf("failed to create sock fd: %s\n", strerror(errno));
        return -1;
    }

    if (connect(sock, p->ai_addr, p->ai_addrlen) < 0) {
        printf("connect to %s error: %s\n", destination, strerror(errno));
        return -1;
    }

    off_t off = 0;
    struct timeval start, end;
    gettimeofday(&start, NULL);
    ssize_t nsend = sendfile(sock, fd, &off, file_size);
    gettimeofday(&end, NULL);
    printf("sendfile send %ld bytes data to %s, taken %g ms\n", nsend, destination, tv_sub_msec_double(end, start));

    sleep(1);

    char *buf = (char *)malloc(file_size);
    memcpy(buf, addr, file_size);

    gettimeofday(&start, NULL);
    ssize_t nwrite = write(sock, buf, file_size);
    gettimeofday(&end, NULL);
    printf("write send %ld bytes data to %s, taken %g ms\n", nwrite, destination, tv_sub_msec_double(end, start));

    return 0;
}
