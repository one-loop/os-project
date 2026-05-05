#ifndef MYSHELL_SOCKET_H
#define MYSHELL_SOCKET_H

#include <stddef.h>

/* TCP byte-stream safe: transfer exactly len bytes, or return -1 (I/O error) or -2 (EOF on recv). */
int send_all(int fd, const void *buf, size_t len);
int recv_all(int fd, void *buf, size_t len);

#endif
