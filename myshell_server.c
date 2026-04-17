#include "myshell_net.h"
#include "executor.h"
#include "pipeline.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// per-client state passed to each worker thread so logs can include the
typedef struct {
    int client_socket;
    int client_id;
    int thread_id;
    char client_ip[INET_ADDRSTRLEN];
    unsigned short client_port;
} client_context_t;

// mutexes keep log output and client numbering stable when threads run at once.
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_client_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_next_client_id = 0;

static void print_server_log(const char *tag, const char *message) {
    pthread_mutex_lock(&g_log_mutex);
    printf("[%s] %s\n", tag, message);
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);
}

static void print_client_log(const char *tag, const client_context_t *ctx, const char *message) {
    pthread_mutex_lock(&g_log_mutex);
    printf("[%s] [Client #%d - %s:%u] %s\n",
           tag,
           ctx->client_id,
           ctx->client_ip,
           ctx->client_port,
           message);
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);
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

    // flush stdio buffers before fork so startup logs are not duplicated into captured output.
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

    // keep prompt formatting clean when command is valid but silent.
    if (used == 0 && response_size > 1) {
        response[0] = '\n';
        response[1] = '\0';
        return 1;
    }

    response[used] = '\0';
    return used;
}

static void *handle_client(void *arg) {
    client_context_t *ctx = (client_context_t *)arg;
    int client_socket = ctx->client_socket;

    // this thread owns exactly one client socket and handles request/response loops.
    for (;;) {
        char cmd_packet[MYSHELL_CMD_MAX];
        memset(cmd_packet, 0, sizeof(cmd_packet));

        int r = recv_all(client_socket, cmd_packet, sizeof(cmd_packet));
        if (r != 0) {
            if (r == -2) {
                char disconnect_msg[128];
                snprintf(disconnect_msg, sizeof(disconnect_msg), "Client #%d disconnected.", ctx->client_id);
                print_server_log("INFO", disconnect_msg);
            } else {
                print_client_log("ERROR", ctx, "Socket receive failed.");
            }
            break;
        }

        char received_msg[512];
        snprintf(received_msg, sizeof(received_msg), "Received command: \"%s\"", cmd_packet);
        print_client_log("RECEIVED", ctx, received_msg);

        // if the client sent "exit", send a goodbye message and break the loop to close the connection. Otherwise, execute the command and send back the response.
        if (strcmp(cmd_packet, "exit") == 0) {
            print_client_log("INFO", ctx, "Client requested disconnect. Closing connection.");

            char bye[MYSHELL_RESP_MAX];
            memset(bye, 0, sizeof(bye));
            snprintf(bye, sizeof(bye), "Disconnected from server.\n");
            (void)send_all(client_socket, bye, sizeof(bye));

            char disconnect_msg[128];
            snprintf(disconnect_msg, sizeof(disconnect_msg), "Client #%d disconnected.", ctx->client_id);
            print_server_log("INFO", disconnect_msg);
            break;
        }

        // log the command being executed for this client before running it, so logs show the command context even if execution fails.
        char executing_msg[512];
        snprintf(executing_msg, sizeof(executing_msg), "Executing command: \"%s\"", cmd_packet);
        print_client_log("EXECUTING", ctx, executing_msg);

        // execute the command and capture the response. If execution fails, the response will contain an error message which we will log and send back to the client.
        char response[MYSHELL_RESP_MAX];
        memset(response, 0, sizeof(response));
        (void)execute_and_capture(cmd_packet, response, sizeof(response));

        // if the response looks like a command-not-found error or other execution error, log it as an error with the original command for context. Otherwise, log it as normal output.
        if (strncmp(response, "Command not found:", 18) == 0 || strncmp(response, "Error:", 6) == 0) {
            char response_log[MYSHELL_RESP_MAX + 1];
            memset(response_log, 0, sizeof(response_log));
            strncpy(response_log, response, sizeof(response_log) - 1);
            response_log[strcspn(response_log, "\r\n")] = '\0';
            
            char server_error_log[512];
            snprintf(server_error_log, sizeof(server_error_log), "Command not found: \"%s\"", cmd_packet);
            print_client_log("ERROR", ctx, server_error_log);
            
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Sending error message to client: \"%s\"", response_log);
            print_client_log("OUTPUT", ctx, error_msg);
        } else { // normal output case
            print_client_log("OUTPUT", ctx, "Sending output to client:");
            if (response[0] != '\0') {
                pthread_mutex_lock(&g_log_mutex);
                printf("%s", response);
                fflush(stdout);
                pthread_mutex_unlock(&g_log_mutex);
            }
        }

        // send the response back to the client. If sending fails, log an error and break the loop to close the connection.
        if (send_all(client_socket, response, sizeof(response)) != 0) {
            print_client_log("ERROR", ctx, "Socket send failed.");
            break;
        }
    }

    close(client_socket);
    free(ctx);
    return NULL;
}

int main(void) {
    // create a socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    // check for fail error
    if (server_socket == -1) {
        printf("socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // allow quick restart after previous run keeps TIME_WAIT sockets.
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    //  define server address structure
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
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

        // create a client context for the new connection and spawn a worker thread to handle it.
        client_context_t *ctx = malloc(sizeof(client_context_t));
        if (ctx == NULL) {
            print_server_log("ERROR", "Memory allocation failed for client context.");
            close(client_socket);
            continue;
        }

        // assign a client ID and thread ID for logging purposes. 
        pthread_mutex_lock(&g_client_id_mutex);
        g_next_client_id++;
        int assigned_client_id = g_next_client_id;
        pthread_mutex_unlock(&g_client_id_mutex);

        // populate the rest of the client context and log the new connection.
        memset(ctx, 0, sizeof(*ctx));
        ctx->client_socket = client_socket;
        ctx->client_id = assigned_client_id;
        ctx->thread_id = assigned_client_id;
        ctx->client_port = ntohs(client_address.sin_port);

        // convert client IP to string for logging. If conversion fails, use "unknown".
        if (inet_ntop(AF_INET, &client_address.sin_addr, ctx->client_ip, sizeof(ctx->client_ip)) == NULL) {
            strncpy(ctx->client_ip, "unknown", sizeof(ctx->client_ip) - 1);
        }

        // log the new connection with client details and assigned thread ID.
        char connect_msg[512];
        snprintf(connect_msg,
                 sizeof(connect_msg),
                 "Client #%d connected from %s:%u. Assigned to Thread-%d.",
                 ctx->client_id,
                 ctx->client_ip,
                 ctx->client_port,
                 ctx->thread_id);
        print_server_log("INFO", connect_msg);

        // create a detached worker thread to handle this client connection.
        if (pthread_create(&tid, NULL, handle_client, ctx) != 0) {
            printf("thread creation failed\n");
            close(client_socket);
            free(ctx);
            continue;
        }

        // detached worker thread owns the client context and socket lifecycle.
        pthread_detach(tid);
    }

    return 0;
}
