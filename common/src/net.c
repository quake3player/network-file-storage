#define _POSIX_C_SOURCE 200809L

#include "net.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int tcp_listen(const char *host, uint16_t port, int backlog) {
    char service[16];
    snprintf(service, sizeof(service), "%u", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *result = NULL;
    int rc = getaddrinfo(host, service, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int listen_fd = -1;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd == -1) {
            continue;
        }

        int opt = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            close(listen_fd);
            listen_fd = -1;
            continue;
        }

        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            if (listen(listen_fd, backlog) == 0) {
                break;
            }
        }

        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(result);
    return listen_fd;
}

int tcp_connect(const char *host, uint16_t port) {
    if (host == NULL) {
        errno = EINVAL;
        return -1;
    }

    char service[16];
    snprintf(service, sizeof(service), "%u", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int rc = getaddrinfo(host, service, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

int recv_all(int fd, void *buf, size_t len) {
    if (fd < 0 || buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    uint8_t *out = (uint8_t *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(fd, out + total, len - total, 0);
        if (n == 0) {
            return (int)total; // connection closed by peer
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        total += (size_t)n;
    }
    return (int)total;
}

int send_all(int fd, const void *buf, size_t len) {
    if (fd < 0 || buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    const uint8_t *data = (const uint8_t *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, data + total, len - total, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            /* If peer closed connection (EPIPE), just return what we sent */
            if (errno == EPIPE) {
                return (int)total;
            }
            return -1;
        }
        total += (size_t)n;
    }
    return (int)total;
}
