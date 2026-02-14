#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static inline void make_socket_pair(int fds[2]) {
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(rc == 0);
}

static inline void write_all_or_fail(int fd, const void *buf, size_t len) {
    const unsigned char *data = (const unsigned char *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, data + total, len - total);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        assert(n > 0);
        total += (size_t)n;
    }
}

static inline size_t read_all_or_fail(int fd, char *buf, size_t cap) {
    size_t total = 0;
    while (total < cap) {
        ssize_t n = read(fd, buf + total, cap - total);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        assert(n >= 0);
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    return total;
}

static inline void assert_contains(const char *haystack, const char *needle) {
    assert(strstr(haystack, needle) != NULL);
}

static inline void close_pair(int fds[2]) {
    close(fds[0]);
    close(fds[1]);
}

#endif
