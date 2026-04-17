#include "myshell_socket.h"

#include <sys/socket.h>
#include <unistd.h>

/*
 * TCP does not guarantee one send()/recv() moves the whole buffer.
 * These helpers loop until len bytes are transferred so command/response frames stay aligned.
 */
int send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int recv_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t got = 0;

    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) {
            return (n == 0) ? -2 : -1;
        }
        got += (size_t)n;
    }
    return 0;
}
