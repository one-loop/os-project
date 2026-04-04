#include "myshell_net.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * TCP does not guarantee one send() / recv() moves the whole buffer.
 * Without looping, the next command/response can share one stream and look "delayed" or blank.
 */
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

static int connect_to_server(void) {
    // create a socket
    // use an int to hold the fd for the socket
    int network_socket;
    // 1st argument: domain/family of the socket. For Internet family of IPv4 addresses, we use AF_INET
    // 2nd argument: type of socket. For TCP sockets, we use SOCK_STREAM
    // 3rd argument: protocol. For TCP, we use 0 (default)
    network_socket = socket(AF_INET, SOCK_STREAM, 0);

    // check for fail error
    if (network_socket == -1) {
        printf("socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // specify an address for the socket we want to connect to
    struct sockaddr_in server_address;

    // specify address family of the socket. AF_INET is the Internet address family for IPv4
    server_address.sin_family = AF_INET;

    // specify the port number of the server we want to connect to.
    // htons converts the port number from host byte order to network byte order for the structure
    server_address.sin_port = htons(MYSHELL_PORT);

    // connect to the shell server on this machine (127.0.0.1 loopback)
    if (inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) != 1) {
        printf("client: invalid server address\n");
        close(network_socket);
        exit(EXIT_FAILURE);
    }

    // connect
    // 1st : socket
    // 2nd : server address structure, cast to a pointer to sockaddr struct so pass the address
    // 3rd : size of the address
    int connection_status =
        connect(network_socket, (struct sockaddr *)&server_address, sizeof(server_address));

    // check for errors with the connection
    if (connection_status == -1) {
        printf("Failed to connect to server. Check that server is running before starting client.\n\n");
        close(network_socket);
        exit(EXIT_FAILURE);
    }

    return network_socket;
}

int main(void) {
    int network_socket = connect_to_server();

    // fixed-size buffer holds one full command line from the user (same idea as local myshell).
    char command[MYSHELL_CMD_MAX];

    // main loop: shell prompt on the client; each line is sent to the server for execution later.
    while (1) {
        printf("$ ");

        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        // remove trailing newline so the server receives the same text a local shell would parse.
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "exit") == 0) {
            break;
        }

        // skip empty lines without contacting the server
        if (command[0] == '\0') {
            continue;
        }

        // pack the command into a fixed-size message (same pattern as the chat example's send buffer).
        char cmd_packet[MYSHELL_CMD_MAX];
        memset(cmd_packet, 0, sizeof(cmd_packet));
        strncpy(cmd_packet, command, sizeof(cmd_packet) - 1);

        if (send_all(network_socket, cmd_packet, sizeof(cmd_packet)) != 0) {
            perror("client: send failed");
            break;
        }

        char server_response[MYSHELL_RESP_MAX + 1];
        memset(server_response, 0, sizeof(server_response));

        int rr = recv_all(network_socket, server_response, (size_t)MYSHELL_RESP_MAX);
        if (rr != 0) {
            if (rr == -2) {
                printf("client: server closed the connection\n");
            } else {
                perror("client: recv failed");
            }
            break;
        }
        server_response[MYSHELL_RESP_MAX] = '\0';

        printf("%s", server_response);
    }

    close(network_socket);
    return 0;
}
