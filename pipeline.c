#include "pipeline.h"
#include "parser.h"
#include "executor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

void run_multi_piped_command(char *command) {
    // function that splits |, creates pipe()s, then forks one child per stage
    // and wirses stdin/stdout/stderr through the pipe chain, parses per-stage 
    // redirections, then waits for all children to finish
    // first pass: count commands by counting separators.
    // number of commands = number of '|' + 1.
    int num_commands = 1;
    char *temp = command;
    while ((temp = strchr(temp, '|')) != NULL) {
        num_commands++;
        temp++;
    }

    // commands[] stores each pipeline stage as a pointer inside the same buffer.
    char *commands[num_commands];
    int cmd_idx = 0;

    // split input by '|' and sanitize whitespace around each stage.
    char *token = strtok(command, "|");
    while (token != NULL && cmd_idx < num_commands) {
        // trim leading spaces.
        while (*token == ' ') token++;

        // trim trailing spaces/newlines.
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }

        // reject invalid cases like "cmd1 || cmd2".
        if (strlen(token) == 0) {
            fprintf(stderr, "Empty command between pipes.\n");
            return;
        }
        commands[cmd_idx++] = token;
        token = strtok(NULL, "|");
    }

    // reject trailing pipe like "cmd1 |".
    if (cmd_idx < num_commands) {
        fprintf(stderr, "Command missing after pipe.\n");
        return;
    }

    // for n commands we need n-1 pipes.
    int num_pipes = num_commands - 1;
    int pipes[num_pipes][2];

    // create all pipe descriptors before forking children.
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Error: pipe() failed while creating pipeline segment");
            return;
        }
    }

    // keep child pids so parent can wait for every stage.
    pid_t pids[num_commands];

    for (int i = 0; i < num_commands; i++) {
        pids[i] = fork();

        if (pids[i] == 0) {
            // child i reads from previous pipe if it is not the first stage.
            if (i > 0) {
                dup2(pipes[i - 1][0], 0);
            }

            // child i writes to next pipe if it is not the last stage.
            if (i < num_commands - 1) {
                dup2(pipes[i][1], 1);
            }

            // close all inherited pipe fds in child to avoid descriptor leaks
            // and hanging readers/writers.
            for (int j = 0; j < num_pipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // each stage can have its own redirections (e.g., cmd2 2>err.log).
            char *outfile = NULL, *infile = NULL, *errfile = NULL;
            if (parse_redirections(commands[i], &outfile, &infile, &errfile) == -1) {
                exit(1);
            }

            // execute this stage with local redirections applied.
            execute_command_with_redirections(commands[i], infile, outfile, errfile);
            exit(1);
        }
    }

    // parent closes all pipe ends; only children should hold active ends now.
    for (int i = 0; i < num_pipes; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // wait for every stage so prompt returns after full pipeline completion.
    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], NULL, 0);
    }
}
