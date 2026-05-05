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

#define TASK_CMD_MAX 256
#define TASK_RESP_MAX 4096
#define QUANTUM_FIRST_ROUND 3
#define QUANTUM_LATER_ROUNDS 7
#define MAX_CLIENT_TRACK 2048
#define RESP_MORE_PREFIX "MORE\n"
#define RESP_DONE_PREFIX "DONE\n"

// per-client state passed to each worker thread so logs can include the
typedef struct {
    int client_socket;
    int client_id;
    int thread_id;
    char client_ip[INET_ADDRSTRLEN];
    unsigned short client_port;
} client_context_t;

// define enum
typedef enum {
    TASK_TYPE_SHELL = 0, // shell commands are one-shot requests
    TASK_TYPE_PROGRAM = 1 // program commands are schedulable tasks
} task_type_t;

// define the enum for a task's state
typedef enum {
    TASK_WAITING = 0,
    TASK_RUNNING = 1,
    TASK_DONE = 2,
    TASK_CANCELLED = 3
} task_state_t;

// define the task control block (TCB)
// contains information about a task and its state (id, clientid, socket, command)
// task type (shell or program), predicted burst/remaining burst, round count, state
typedef struct task {
    int task_id;
    int client_id;
    int client_socket;

    task_type_t type;
    task_state_t state;

    char command[TASK_CMD_MAX];

    // scheduling fields
    int predicted_burst; // for unknown programs, use default
    int remaining_burst; // decremented by scheduler
    int executed_units; // total simulated units already executed
    int rounds_executed; // round 0 -> first quantum
    int demo_completion_line_written; // ensures Demo N/N line is appended once
    unsigned long arrival_seq; // fcfs tie-breaker


    // result delivery back to client-thread
    char response[TASK_RESP_MAX];
    int response_ready;
    int task_finished;
    int cancel_requested;

    pthread_mutex_t response_mutex;
    pthread_cond_t done_cv;
    pthread_cond_t chunk_consumed_cv;

    struct task *next; // queue linkage
} task_t;

// forward declaration because scheduler loop calls this helper before its definition.
static size_t execute_and_capture(const char *command, char *response, size_t response_size);
static void print_server_log(const char *tag, const char *message);
static void print_short_client_cmd(const client_context_t *ctx, const char *cmd);
static void print_short_client_status(const client_context_t *ctx, const char *status);
static void print_short_task_line(const task_t *task, const char *label, int value);
static void append_sched_history(const task_t *task);
static void print_sched_summary_if_idle(void);

// global counters for ids/sequencing
static pthread_mutex_t g_task_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_next_task_id = 0;

static pthread_mutex_t g_arrival_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_next_arrival_seq = 0;

// global waiting queue and synchronization for scheduler producer/consumer flow.
static pthread_mutex_t g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_queue_not_empty = PTHREAD_COND_INITIALIZER;
static task_t *g_queue_head = NULL;
static task_t *g_queue_tail = NULL;
static size_t g_queue_size = 0;
static int g_last_scheduled_task_id = -1;
static task_t *g_running_task = NULL;

// client connection map allows scheduler to cancel/skip tasks for disconnected clients.
static pthread_mutex_t g_client_conn_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_client_connected[MAX_CLIENT_TRACK];

// default predicted runtime used for generic program tasks.
#define DEFAULT_PROGRAM_BURST 10


// helper functions to assign unique ids and arrival sequence numbers
static int next_task_id(void) {
    pthread_mutex_lock(&g_task_id_mutex);
    int id = ++g_next_task_id;
    pthread_mutex_unlock(&g_task_id_mutex);
    return id;
}

static unsigned long next_arrival_seq(void) {
    pthread_mutex_lock(&g_arrival_mutex);
    unsigned long seq = ++g_next_arrival_seq;
    pthread_mutex_unlock(&g_arrival_mutex);
    return seq;
}

// parse commands in the form "demo N" where N is a positive integer.
// returns 1 on success and writes N to out_n, otherwise returns 0.
static int parse_demo_burst(const char *cmd, int *out_n) {
    const char *prefix = "demo ";
    size_t prefix_len = strlen(prefix);
    if (strncmp(cmd, prefix, prefix_len) != 0) {
        return 0;
    }

    char *endptr = NULL;
    long value = strtol(cmd + prefix_len, &endptr, 10);
    if (endptr == cmd + prefix_len || *endptr != '\0' || value <= 0) {
        return 0;
    }

    *out_n = (int)value;
    return 1;
}

// parse commands in the form "./demo N" where N is a positive integer.
// returns 1 on success and writes N to out_n, otherwise returns 0.
static int parse_dot_demo_burst(const char *cmd, int *out_n) {
    const char *prefix = "./demo ";
    size_t prefix_len = strlen(prefix);
    if (strncmp(cmd, prefix, prefix_len) != 0) {
        return 0;
    }

    char *endptr = NULL;
    long value = strtol(cmd + prefix_len, &endptr, 10);
    if (endptr == cmd + prefix_len || *endptr != '\0' || value <= 0) {
        return 0;
    }

    *out_n = (int)value;
    return 1;
}

// classify a received command into shell or program and assign predicted burst.
// this keeps parsing policy in one place so scheduler logic can rely on it later.
static void classify_request(const char *cmd, task_type_t *type, int *predicted_burst) {
    int demo_n = 0;
    if (parse_demo_burst(cmd, &demo_n) || parse_dot_demo_burst(cmd, &demo_n)) {
        *type = TASK_TYPE_PROGRAM;
        *predicted_burst = demo_n;
        return;
    }

    // treat common executable-style forms as program tasks with default burst.
    if (strncmp(cmd, "./", 2) == 0) {
        *type = TASK_TYPE_PROGRAM;
        *predicted_burst = DEFAULT_PROGRAM_BURST;
        return;
    }

    // everything else is handled as a normal shell command.
    *type = TASK_TYPE_SHELL;
    *predicted_burst = -1;
}

// create one heap-allocated task control block from a client request line.
// caller owns this task and is responsible for eventually freeing it.
static task_t *task_create_from_command(const client_context_t *ctx, const char *cmd_packet) {
    task_t *task = (task_t *)malloc(sizeof(task_t));
    if (task == NULL) {
        return NULL;
    }

    memset(task, 0, sizeof(*task));
    task->task_id = next_task_id();
    task->client_id = ctx->client_id;
    task->client_socket = ctx->client_socket;
    task->state = TASK_WAITING;
    task->rounds_executed = 0;
    task->executed_units = 0;
    task->demo_completion_line_written = 0;
    task->arrival_seq = next_arrival_seq();
    task->response_ready = 0;
    task->task_finished = 0;
    task->cancel_requested = 0;
    task->next = NULL;

    strncpy(task->command, cmd_packet, sizeof(task->command) - 1);
    classify_request(task->command, &task->type, &task->predicted_burst);
    task->remaining_burst = task->predicted_burst;

    pthread_mutex_init(&task->response_mutex, NULL);
    pthread_cond_init(&task->done_cv, NULL);
    pthread_cond_init(&task->chunk_consumed_cv, NULL);

    // short created line for task lifecycle visibility.
    print_short_task_line(task, "created", task->remaining_burst);

    return task;
}

// queue helper that appends task at tail in O(1) and notifies scheduler.
static void scheduler_enqueue_task(task_t *task, int log_enqueued) {
    pthread_mutex_lock(&g_queue_mutex);
    task->next = NULL;
    if (g_queue_tail == NULL) {
        g_queue_head = task;
        g_queue_tail = task;
    } else {
        g_queue_tail->next = task;
        g_queue_tail = task;
    }
    g_queue_size++;
    pthread_cond_signal(&g_queue_not_empty);
    pthread_mutex_unlock(&g_queue_mutex);

    (void)log_enqueued;
}

// queue helper that removes and returns the head task in O(1).
// returns NULL if the queue is empty.
// remove one task from the queue using a combined policy:
// 1) shell commands always win (fast-track immediate handling)
// 2) among program tasks, choose shortest remaining time (sjrf)
// 3) if same remaining time, use fcfs via arrival sequence
// 4) avoid scheduling the same task id consecutively unless it is the only candidate
static task_t *scheduler_select_next_task_locked(void) {
    task_t *curr = g_queue_head;
    task_t *prev = NULL;

    task_t *best = NULL;
    task_t *best_prev = NULL;

    int candidate_count = 0;
    int shell_count = 0;

    while (curr != NULL) {
        int eligible = 1;
        if (curr->task_id == g_last_scheduled_task_id) {
            eligible = 0;
        }

        // count all candidates regardless of eligibility, to detect single-candidate cases.
        candidate_count++;
        if (curr->type == TASK_TYPE_SHELL) {
            shell_count++;
        }

        if (eligible) {
            if (best == NULL) {
                best = curr;
                best_prev = prev;
            } else {
                // shell commands are always selected before programs.
                if (curr->type == TASK_TYPE_SHELL && best->type != TASK_TYPE_SHELL) {
                    best = curr;
                    best_prev = prev;
                } else if (curr->type == best->type) {
                    if (curr->type == TASK_TYPE_SHELL) {
                        // for shell requests with same class, preserve fcfs ordering.
                        if (curr->arrival_seq < best->arrival_seq) {
                            best = curr;
                            best_prev = prev;
                        }
                    } else {
                        // program tasks use sjrf first, then fcfs tie-break.
                        if (curr->remaining_burst < best->remaining_burst ||
                            (curr->remaining_burst == best->remaining_burst &&
                             curr->arrival_seq < best->arrival_seq)) {
                            best = curr;
                            best_prev = prev;
                        }
                    }
                }
            }
        }

        prev = curr;
        curr = curr->next;
    }

    // if no eligible task exists due to "no consecutive same task" rule, allow the only choice.
    if (best == NULL && candidate_count == 1) {
        best = g_queue_head;
        best_prev = NULL;
    } else if (best == NULL && shell_count == 1 && g_queue_head != NULL && g_queue_head->next == NULL) {
        best = g_queue_head;
        best_prev = NULL;
    }

    if (best == NULL) {
        return NULL;
    }

    // unlink selected task from queue.
    if (best_prev == NULL) {
        g_queue_head = best->next;
    } else {
        best_prev->next = best->next;
    }
    if (best == g_queue_tail) {
        g_queue_tail = best_prev;
    }
    best->next = NULL;
    if (g_queue_size > 0) {
        g_queue_size--;
    }

    // record which task the scheduler selected before the caller executes it.

    return best;
}

// destroy per-task synchronization primitives before freeing the task.
static void task_destroy(task_t *task) {
    if (task == NULL) {
        return;
    }
    pthread_mutex_destroy(&task->response_mutex);
    pthread_cond_destroy(&task->done_cv);
    pthread_cond_destroy(&task->chunk_consumed_cv);
    free(task);
}

// publish one response frame for a task.
// the consumer thread sends this chunk and then acknowledges consumption.
static void publish_task_chunk(task_t *task, const char *payload, int finished) {
    pthread_mutex_lock(&task->response_mutex);
    while (task->response_ready && !task->cancel_requested) {
        pthread_cond_wait(&task->chunk_consumed_cv, &task->response_mutex);
    }

    memset(task->response, 0, sizeof(task->response));
    snprintf(task->response,
             sizeof(task->response),
             "%s%s",
             finished ? RESP_DONE_PREFIX : RESP_MORE_PREFIX,
             payload != NULL ? payload : "");
    task->response_ready = 1;
    task->task_finished = finished ? 1 : 0;
    pthread_cond_signal(&task->done_cv);
    pthread_mutex_unlock(&task->response_mutex);
}

// mark one client id as connected/disconnected for cancellation checks.
static void set_client_connected(int client_id, int connected) {
    if (client_id <= 0 || client_id >= MAX_CLIENT_TRACK) {
        return;
    }
    pthread_mutex_lock(&g_client_conn_mutex);
    g_client_connected[client_id] = connected;
    pthread_mutex_unlock(&g_client_conn_mutex);
}

// read client connection state used by scheduler before/while execution.
static int is_client_connected(int client_id) {
    int connected = 0;
    if (client_id <= 0 || client_id >= MAX_CLIENT_TRACK) {
        return 0;
    }
    pthread_mutex_lock(&g_client_conn_mutex);
    connected = g_client_connected[client_id];
    pthread_mutex_unlock(&g_client_conn_mutex);
    return connected;
}

// complete a task as cancelled and wake any client thread waiting on this task.
static void complete_task_cancelled(task_t *task, const char *reason) {
    task->state = TASK_CANCELLED;
    task->cancel_requested = 1;
    char cancel_payload[256];
    snprintf(cancel_payload, sizeof(cancel_payload), "task #%d cancelled: %s\n", task->task_id, reason);
    publish_task_chunk(task, cancel_payload, 1);

    // cancelled tasks are logged once the state change is visible to waiters.
    char cancel_msg[256];
    snprintf(cancel_msg, sizeof(cancel_msg), "cancelled: %s", reason);
}

// remove all waiting tasks for one client and mark running task for cancellation.
// this satisfies the requirement that server drops tasks after client disconnect.
static void cancel_tasks_for_client(int client_id) {
    task_t *cancel_head = NULL;
    task_t *cancel_tail = NULL;

    pthread_mutex_lock(&g_queue_mutex);

    // request cancellation for a currently running task owned by this client.
    if (g_running_task != NULL && g_running_task->client_id == client_id) {
        g_running_task->cancel_requested = 1;
    }

    // remove matching waiting tasks from queue and move them to a local cancel list.
    task_t *curr = g_queue_head;
    task_t *prev = NULL;
    while (curr != NULL) {
        task_t *next = curr->next;
        if (curr->client_id == client_id) {
            if (prev == NULL) {
                g_queue_head = next;
            } else {
                prev->next = next;
            }
            if (curr == g_queue_tail) {
                g_queue_tail = prev;
            }
            curr->next = NULL;
            if (g_queue_size > 0) {
                g_queue_size--;
            }

            if (cancel_tail == NULL) {
                cancel_head = curr;
                cancel_tail = curr;
            } else {
                cancel_tail->next = curr;
                cancel_tail = curr;
            }
        } else {
            prev = curr;
        }
        curr = next;
    }

    pthread_mutex_unlock(&g_queue_mutex);

    // wake waiting client threads outside queue lock to keep lock scope short.
    curr = cancel_head;
    while (curr != NULL) {
        task_t *next = curr->next;
        complete_task_cancelled(curr, "client disconnected before execution");
        curr = next;
    }
}

// check if current program task should be preempted at tick boundary.
// returns 1 if a shell task exists or a shorter program is waiting.
static int should_preempt_program_task(task_t *current_task) {
    int preempt = 0;
    pthread_mutex_lock(&g_queue_mutex);
    task_t *iter = g_queue_head;
    while (iter != NULL) {
        if (iter->type == TASK_TYPE_SHELL) {
            preempt = 1;
            break;
        }
        if (iter->type == TASK_TYPE_PROGRAM && iter->remaining_burst < current_task->remaining_burst) {
            preempt = 1;
            break;
        }
        iter = iter->next;
    }
    pthread_mutex_unlock(&g_queue_mutex);
    return preempt;
}

// simulate one scheduled time slice for a program task and append per-tick output.
// each tick represents one time unit and runs for one second to visualize scheduling.
static void run_program_slice(task_t *task, char *response, size_t response_size) {
    (void)response;
    (void)response_size;
    int quantum = (task->rounds_executed == 0) ? QUANTUM_FIRST_ROUND : QUANTUM_LATER_ROUNDS;
    int slice = quantum;
    if (task->remaining_burst < slice) {
        slice = task->remaining_burst;
    }

    // show the current time slice configuration before execution.

    for (int i = 0; i < slice; i++) {
        // if client disconnected while task is running, stop immediately.
        if (!is_client_connected(task->client_id) || task->cancel_requested) {
            task->state = TASK_CANCELLED;
            break;
        }

        char tick_payload[128];
        snprintf(tick_payload,
                 sizeof(tick_payload),
                 "Demo %d/%d\n",
                 task->executed_units,
                 task->predicted_burst);
        // stream one line per tick to client as required by assignment.
        publish_task_chunk(task, tick_payload, 0);

        task->executed_units++;
        task->remaining_burst--;

        // this sleep makes the scheduler behavior visible in logs/demo output.
        sleep(1);

        // evaluate selective preemption at each tick boundary.
        if (task->remaining_burst > 0 && should_preempt_program_task(task)) {
            // selective preemption is part of the scheduler policy for Phase 4.
            break;
        }
    }

    task->rounds_executed++;
    if (task->state == TASK_CANCELLED) {
        // state already set by cancellation path.
    } else if (task->remaining_burst <= 0) {
        // append the final Demo N/N line exactly once when execution fully completes.
        if (!task->demo_completion_line_written) {
            char completion_payload[128];
            snprintf(completion_payload,
                     sizeof(completion_payload),
                     "Demo %d/%d\n",
                     task->predicted_burst,
                     task->predicted_burst);
            publish_task_chunk(task, completion_payload, 1);
            task->demo_completion_line_written = 1;
        } else {
            publish_task_chunk(task, "\n", 1);
        }
        task->state = TASK_DONE;
    } else {
        task->state = TASK_WAITING;
        // unfinished task is requeued; do not mark command done yet.
    }
}

// scheduler thread is the only executor to simulate one shared cpu.
// it waits for tasks, picks one by policy, runs one slice, then completes or requeues.
static void *scheduler_loop(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&g_queue_mutex);
        while (g_queue_size == 0) {
            pthread_cond_wait(&g_queue_not_empty, &g_queue_mutex);
        }
        task_t *task = scheduler_select_next_task_locked();
        if (task != NULL) {
            g_running_task = task;
        }
        pthread_mutex_unlock(&g_queue_mutex);

        if (task == NULL) {
            continue;
        }

        task->state = TASK_RUNNING;
        g_last_scheduled_task_id = task->task_id;

        // print start for first dispatch and running for resumed dispatches.
        if (task->rounds_executed == 0) {
            print_short_task_line(task, "started", task->predicted_burst);
        } else {
            print_short_task_line(task, "running", task->remaining_burst);
        }

        // if client already disconnected, cancel before spending cpu time.
        if (!is_client_connected(task->client_id)) {
            complete_task_cancelled(task, "client disconnected");
            pthread_mutex_lock(&g_queue_mutex);
            g_running_task = NULL;
            pthread_mutex_unlock(&g_queue_mutex);
            continue;
        }

        if (task->type == TASK_TYPE_SHELL) {
            // shell commands are executed as one-shot non-preemptive tasks.
            char shell_payload[TASK_RESP_MAX];
            memset(shell_payload, 0, sizeof(shell_payload));
            (void)execute_and_capture(task->command, shell_payload, sizeof(shell_payload));
            publish_task_chunk(task, shell_payload, 1);
            task->state = TASK_DONE;
        } else {
            // program tasks run for one quantum and may be requeued if unfinished.
            run_program_slice(task, NULL, 0);
        }

        if (task->state == TASK_DONE) {
            // completion is signaled here; final response delivery is logged by client thread.
            if (task->type == TASK_TYPE_PROGRAM) {
                append_sched_history(task);
            }
        } else if (task->state == TASK_CANCELLED) {
            complete_task_cancelled(task, "client disconnected while running");
            if (task->type == TASK_TYPE_PROGRAM) {
                append_sched_history(task);
            }
        } else {
            // unfinished program tasks are pushed back for future rounds.
            append_sched_history(task);
            print_short_task_line(task, "waiting", task->remaining_burst);
            scheduler_enqueue_task(task, 0);
        }

        pthread_mutex_lock(&g_queue_mutex);
        if (g_running_task == task) {
            g_running_task = NULL;
        }
        pthread_mutex_unlock(&g_queue_mutex);

    }
    return NULL;
}





// mutexes keep log output and client numbering stable when threads run at once.
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_client_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_next_client_id = 0;

static void print_server_log(const char *tag, const char *message) {
    // serialize prints so log lines from concurrent threads do not interleave.
    pthread_mutex_lock(&g_log_mutex);
    printf("[%s] %s\n", tag, message);
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);
}

static void print_client_log(const char *tag, const client_context_t *ctx, const char *message) {
    // include stable client identity in every per-client log message.
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

// short command line: "[5] >>> ./demo 12"
static void print_short_client_cmd(const client_context_t *ctx, const char *cmd) {
    pthread_mutex_lock(&g_log_mutex);
    printf("[%d] >>> %s\n", ctx->client_id, cmd);
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);
}

// short status line: "[5] <<< client connected".
static void print_short_client_status(const client_context_t *ctx, const char *status) {
    pthread_mutex_lock(&g_log_mutex);
    printf("[%d] <<< %s\n", ctx->client_id, status);
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);
}

// short task line: "(5) --- created (12)".
static void print_short_task_line(const task_t *task, const char *label, int value) {
    pthread_mutex_lock(&g_log_mutex);
    printf("(%d) --- %s (%d)\n", task->client_id, label, value);
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);
}

// scheduling history printed as a compact summary line.
static pthread_mutex_t g_sched_hist_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_sched_history[4096];
static size_t g_sched_history_len = 0;

static void append_sched_history(const task_t *task) {
    pthread_mutex_lock(&g_sched_hist_mutex);
    int n = snprintf(g_sched_history + g_sched_history_len,
                     sizeof(g_sched_history) - g_sched_history_len,
                     "P%d-(%d)-",
                     task->client_id,
                     task->remaining_burst);
    if (n > 0 && (size_t)n < sizeof(g_sched_history) - g_sched_history_len) {
        g_sched_history_len += (size_t)n;
    }
    pthread_mutex_unlock(&g_sched_hist_mutex);
}

static void print_sched_summary_if_idle(void) {
    pthread_mutex_lock(&g_queue_mutex);
    int empty = (g_queue_size == 0 && g_running_task == NULL);
    pthread_mutex_unlock(&g_queue_mutex);

    pthread_mutex_lock(&g_sched_hist_mutex);
    if (empty && g_sched_history_len > 0) {
        // trim trailing dash
        if (g_sched_history_len > 0 && g_sched_history[g_sched_history_len - 1] == '-') {
            g_sched_history[g_sched_history_len - 1] = '\0';
        }
        pthread_mutex_lock(&g_log_mutex);
        printf("%s\n", g_sched_history);
        fflush(stdout);
        pthread_mutex_unlock(&g_log_mutex);
        // reset history after summary is printed.
        g_sched_history_len = 0;
        g_sched_history[0] = '\0';
    }
    pthread_mutex_unlock(&g_sched_hist_mutex);
}

static int send_all(int fd, const void *buf, size_t len) {
    // transmit exactly len bytes even if kernel accepts partial writes.
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
    // receive exactly len bytes to keep the fixed-size protocol in sync.
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
        // convert internal failures into a user-visible server error message.
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

        // return from child without flushing parent-owned stdio state.
        _exit(0);
    }

    // parent: read captured output, then wait for command child to finish.
    close(cap_pipe[1]);

    size_t used = 0;
    while (used < response_size - 1) {
        // stop reading when buffer is full or the capture pipe reaches eof.
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
    // recover thread-owned context passed from accept loop.
    client_context_t *ctx = (client_context_t *)arg;
    int client_socket = ctx->client_socket;

    // this thread owns exactly one client socket and handles request/response loops.
    for (;;) {
        char cmd_packet[MYSHELL_CMD_MAX];
        memset(cmd_packet, 0, sizeof(cmd_packet));

        int r = recv_all(client_socket, cmd_packet, sizeof(cmd_packet));
        if (r != 0) {
            // mark client disconnected and remove/cancel any pending tasks.
            set_client_connected(ctx->client_id, 0);
            cancel_tasks_for_client(ctx->client_id);
            if (r == -2) {
                print_short_client_status(ctx, "client disconnected");
            } else {
                print_client_log("ERROR", ctx, "Socket receive failed.");
            }
            break;
        }

        // print one short command line for each received request.
        print_short_client_cmd(ctx, cmd_packet);

        // if the client sent "exit", send a goodbye message and break the loop to close the connection. Otherwise, execute the command and send back the response.
        if (strcmp(cmd_packet, "exit") == 0) {
            set_client_connected(ctx->client_id, 0);
            cancel_tasks_for_client(ctx->client_id);

            char bye[MYSHELL_RESP_MAX];
            memset(bye, 0, sizeof(bye));
            snprintf(bye, sizeof(bye), "Disconnected from server.\n");
            (void)send_all(client_socket, bye, sizeof(bye));

            print_short_client_status(ctx, "client disconnected");
            break;
        }

        // create and classify one tcb entry for this request.
        task_t *task = task_create_from_command(ctx, cmd_packet);
        if (task == NULL) {
            print_client_log("ERROR", ctx, "Task allocation failed.");
            break;
        }

        // enqueue into global waiting queue; scheduler thread will execute it.
        scheduler_enqueue_task(task, 1);

        // stream chunks until scheduler marks this task as finished.
        int finished = 0;
        while (!finished) {
            char out_chunk[TASK_RESP_MAX];
            memset(out_chunk, 0, sizeof(out_chunk));

            pthread_mutex_lock(&task->response_mutex);
            while (!task->response_ready) {
                pthread_cond_wait(&task->done_cv, &task->response_mutex);
            }
            memcpy(out_chunk, task->response, sizeof(out_chunk));
            finished = task->task_finished;
            task->response_ready = 0;
            pthread_cond_signal(&task->chunk_consumed_cv);
            pthread_mutex_unlock(&task->response_mutex);

            // send each streamed frame back to the client.
            if (send_all(client_socket, out_chunk, sizeof(out_chunk)) != 0) {
                print_client_log("ERROR", ctx, "Socket send failed.");
                set_client_connected(ctx->client_id, 0);
                cancel_tasks_for_client(ctx->client_id);
                finished = 1;
                break;
            }
        }

        // print concise delivery line and final state after sending response.
        size_t payload_len = strnlen(task->response, sizeof(task->response));
        pthread_mutex_lock(&g_log_mutex);
        printf("[%d] <<< %zu bytes sent\n", ctx->client_id, payload_len);
        fflush(stdout);
        pthread_mutex_unlock(&g_log_mutex);
        print_short_task_line(task, "ended", task->remaining_burst);

        // print scheduling summary after response delivery once execution is truly idle.
        if (task->type == TASK_TYPE_PROGRAM) {
            print_sched_summary_if_idle();
        }

        // client thread owns final cleanup after response delivery.
        task_destroy(task);
    }

    close(client_socket);
    // free heap-allocated context created by main accept loop.
    free(ctx);
    return NULL;
}

int main(void) {
    pthread_t scheduler_tid;
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

    pthread_mutex_lock(&g_log_mutex);
    printf("------------------------------\n");
    printf("| Hello, Server Started |\n");
    printf("------------------------------\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_log_mutex);

    // start scheduler thread before accepting clients so submitted tasks have a consumer.
    if (pthread_create(&scheduler_tid, NULL, scheduler_loop, NULL) != 0) {
        print_server_log("ERROR", "Failed to create scheduler thread.");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    pthread_detach(scheduler_tid);

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
        set_client_connected(ctx->client_id, 1);

        // convert client IP to string for logging. If conversion fails, use "unknown".
        if (inet_ntop(AF_INET, &client_address.sin_addr, ctx->client_ip, sizeof(ctx->client_ip)) == NULL) {
            strncpy(ctx->client_ip, "unknown", sizeof(ctx->client_ip) - 1);
        }

        // log one concise client connection line.
        print_short_client_status(ctx, "client connected");

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
