#include "myshell_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

static int send_all(int fd, const void *buf, size_t len) {
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

static int recv_all(int fd, void *buf, size_t len) {
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

/*
 * Minimal stub server so the myshell client can connect and loop.
 * TODO: replace the body of the loop with parse/execute and real output.
 */
int main(void) {
    // create a socket
    int server_socket;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    // check for fail error
    if (server_socket == -1) {
        printf("socket creation failed\n");
        exit(EXIT_FAILURE);
    } else {
        printf("Server: socket CREATION successful\n");
    }

    // allow quick restart after the previous process still holds TIME_WAIT 
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // define server address structure
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(MYSHELL_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    // bind the socket to the specified IP and port
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        printf("socket binding failed\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    } else {
        printf("Server: socket BIND successful\n");
    }

    // after it is bound, we can listen for connections
    // 2nd : how many connections can be waiting for this socket at one point in time so at least 1
    if (listen(server_socket, 5) < 0) {
        printf("socket listening failed\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    } else {
        printf("Server: socket LISTEN successful\n");
    }

    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);

    // when we accept a connection, we get back the client socket which we will read/write on
    int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_len);

    if (client_socket < 0) {
        printf("socket accepting failed\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    } else {
        printf("Server: socket ACCEPT successful\n");
    }

    // parent listening socket is not needed for single-client stub; close it so the port is not held twice
    close(server_socket);

    // main loop: server listens for commands from the client and executes them
    for (;;) {
        char cmd_packet[MYSHELL_CMD_MAX];
        memset(cmd_packet, 0, sizeof(cmd_packet));

        int r = recv_all(client_socket, cmd_packet, sizeof(cmd_packet));
        if (r != 0) {
            break;
        }

        // TODO: execute cmd_packet like local myshell and fill response from real output.
        // parse the command string -- redirections, tokenize, fork(), child applies dup2() for stdin/stdout/stderr files
        // runs run_builtin() if the command is a builtin, otherwise execvp() the command
        // args becomes the argv array passed into execvp.
        char response[MYSHELL_RESP_MAX];
        memset(response, 0, sizeof(response));
        snprintf(response, sizeof(response), "[myshell server stub] received command (not executed yet): %s\n", cmd_packet);

        if (send_all(client_socket, response, sizeof(response)) != 0) {
            perror("server: send failed");
            break;
        }
    }

    close(client_socket);
    return 0;
}
