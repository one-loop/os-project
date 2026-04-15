#include "myshell_net.h"
#include "executor.h"
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <errno.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

static void print_server_log(const char *tag, const char *message) {
    printf("[%s] %s\n", tag, message);
    fflush(stdout);
}

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
 * run one command using the same local shell execution path and capture
 * everything printed to stdout/stderr into response.
 * returns number of bytes stored in response (without relying on trailing zeros).
 */
static size_t execute_and_capture(const char *command, char *response, size_t response_size) {
    // create a pipe used only for capturing child output.
    int cap_pipe[2];
    if (pipe(cap_pipe) < 0) {
        snprintf(response, response_size, "server error: capture pipe failed\n");
        return strnlen(response, response_size);
    }

    // flush stdio buffers before fork so prior server prints are not duplicated
    // into the capture pipe by child-side buffered flush.
    fflush(NULL);

    pid_t pid = fork();
    if (pid < 0) {
        close(cap_pipe[0]);
        close(cap_pipe[1]);
        snprintf(response, response_size, "server error: fork failed\n");
        return strnlen(response, response_size);
    }

    if (pid == 0) {
        // child: redirect stdout/stderr to capture pipe and execute command path.
        close(cap_pipe[0]);

        if (dup2(cap_pipe[1], STDOUT_FILENO) < 0) {
            _exit(1);
        }
        if (dup2(cap_pipe[1], STDERR_FILENO) < 0) {
            _exit(1);
        }
        close(cap_pipe[1]);

        // copy command into mutable buffer because parser/executor mutates strings.
        char cmd_buf[MYSHELL_CMD_MAX];
        memset(cmd_buf, 0, sizeof(cmd_buf));
        strncpy(cmd_buf, command, sizeof(cmd_buf) - 1);

        // same dispatch as local main.c
        if (strchr(cmd_buf, '|') != NULL) {
            run_multi_piped_command(cmd_buf);
        } else {
            run_command(cmd_buf);
        }

        _exit(0);
    }

    // parent: read captured output, then wait for command child to finish.
    close(cap_pipe[1]);

    size_t used = 0;
    while (used < response_size - 1) {
        ssize_t n = read(cap_pipe[0], response + used, response_size - 1 - used);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
    }

    close(cap_pipe[0]);
    waitpid(pid, NULL, 0);

    // if command produced no output, return a newline so prompt formatting stays clean on client.
    if (used == 0 && response_size > 1) {
        response[0] = '\n';
        response[1] = '\0';
        return 1;
    }

    response[used] = '\0';
    return used;
}


void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg); // free the memory allocated for the client socket (each thread gets its own copy of the socket value safely)

    // main loop: server listens for commands from the client and executes them.
    for (;;) {
        char cmd_packet[MYSHELL_CMD_MAX];
        memset(cmd_packet, 0, sizeof(cmd_packet));

        int r = recv_all(client_socket, cmd_packet, sizeof(cmd_packet));
        if (r != 0) break;

        // if client asks to exit, close this session loop cleanly.
        if (strcmp(cmd_packet, "exit") == 0) {
            char bye[MYSHELL_RESP_MAX];
            memset(bye, 0, sizeof(bye));
            snprintf(bye, sizeof(bye), "bye\n");
            (void)send_all(client_socket, bye, sizeof(bye));
            break;
        }

        char received_msg[512];
        snprintf(received_msg, sizeof(received_msg), "Received command: \"%s\" from client.", cmd_packet);
        print_server_log("RECEIVED", received_msg);

        char executing_msg[512];
        snprintf(executing_msg, sizeof(executing_msg), "Executing command: \"%s\"", cmd_packet);
        print_server_log("EXECUTING", executing_msg);

        // execute command using the same shell code and capture printable output.
        char response[MYSHELL_RESP_MAX];
        memset(response, 0, sizeof(response));
        (void)execute_and_capture(cmd_packet, response, sizeof(response));

        // mirror the screenshot style: show whether we are sending normal output or an error.
        if (strncmp(response, "Command not found:", 18) == 0 || strncmp(response, "Error:", 6) == 0) {
            char response_log[MYSHELL_RESP_MAX + 1];
            memset(response_log, 0, sizeof(response_log));
            strncpy(response_log, response, sizeof(response_log) - 1);
            response_log[strcspn(response_log, "\r\n")] = '\0';

            char server_error_log[512];
            snprintf(server_error_log, sizeof(server_error_log), "Command not found: \"%s\"", cmd_packet);
            print_server_log("ERROR", server_error_log);
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Sending error message to client: \"%s\"", response_log);
            print_server_log("OUTPUT", error_msg);
        } else {
            print_server_log("OUTPUT", "Sending output to client:");
            if (response[0] != '\0') {
                printf("%s", response);
                fflush(stdout);
            }
        }

        if (send_all(client_socket, response, sizeof(response)) != 0) {
            perror("server: send failed");
            break;
        }
    }

    // close the client socket
    close(client_socket);
    return NULL;
}

int main(void) {
    // create a socket
    int server_socket;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    // check for fail error
    if (server_socket == -1) {
        printf("socket creation failed\n");
        exit(EXIT_FAILURE);
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
    }

    // after it is bound, we can listen for connections
    // 2nd : how many connections can be waiting for this socket at one point in time so at least 1
    if (listen(server_socket, 5) < 0) {
        printf("socket listening failed\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    print_server_log("INFO", "Server started, waiting for client connections...");

    int num_clients = 0;

    while (1) {
        pthread_t tid;
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);

        // accept a connection from a client
        // when we accept a connection, we get back the client socket which we will read/write on
        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_len);

        if (client_socket < 0) {
            printf("socket accepting failed\n");
            close(server_socket);
            exit(EXIT_FAILURE);
        }

        // allocate memory for the client socket
        int *pclient = malloc(sizeof(int));
        *pclient = client_socket;

        // create a new thread to handle the client (so multiple clients can be handled concurrently)
        // (each thread gets its own copy of the client socket value safely)
        if (pthread_create(&tid, NULL, handle_client, pclient) != 0) {
            // if thread creation fails, close the client socket 
            // and continue accepting further clients
            printf("thread creation failed\n");
            close(client_socket);
        } else {
            num_clients++;
            printf("[INFO] Client %d connected. Assigned to Thread\n", num_clients);
        }
        // detach the thread so it can run independently
        pthread_detach(tid);

    }
    
    return 0;
}
