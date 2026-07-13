#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>

int tcp_listen(const char *host, uint16_t port, int backlog);
int tcp_connect(const char *host, uint16_t port);
int recv_all(int fd, void *buf, size_t len);
int send_all(int fd, const void *buf, size_t len);

#endif
