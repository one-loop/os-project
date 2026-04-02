#include "pipeline.h"
#include "parser.h"
#include "executor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

void run_multi_piped_command(char *command) {
    int num_commands = 1;
    char *temp = command;
    while ((temp = strchr(temp, '|')) != NULL) {
        num_commands++;
        temp++;
    }

    char *commands[num_commands];
    int cmd_idx = 0;
    char *token = strtok(command, "|");
    while (token != NULL && cmd_idx < num_commands) {
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        if (strlen(token) == 0) {
            fprintf(stderr, "Empty command between pipes.\n");
            return;
        }
        commands[cmd_idx++] = token;
        token = strtok(NULL, "|");
    }

    if (cmd_idx < num_commands) {
        fprintf(stderr, "Command missing after pipe.\n");
        return;
    }

    int num_pipes = num_commands - 1;
    int pipes[num_pipes][2];

    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Error: pipe() failed while creating pipeline segment");
            return;
        }
    }

    pid_t pids[num_commands];

    for (int i = 0; i < num_commands; i++) {
        pids[i] = fork();

        if (pids[i] == 0) {
            if (i > 0) {
                dup2(pipes[i - 1][0], 0);
            }

            if (i < num_commands - 1) {
                dup2(pipes[i][1], 1);
            }

            for (int j = 0; j < num_pipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            char *outfile = NULL, *infile = NULL, *errfile = NULL;
            if (parse_redirections(commands[i], &outfile, &infile, &errfile) == -1) {
                exit(1);
            }

            execute_command_with_redirections(commands[i], infile, outfile, errfile);
            exit(1);
        }
    }

    for (int i = 0; i < num_pipes; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], NULL, 0);
    }
}
