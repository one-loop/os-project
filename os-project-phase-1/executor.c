#include "executor.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_ARGS 64

void run_command(char *command) {
    char *args[MAX_ARGS];
    char *outfile = NULL;
    char *infile = NULL;
    char *errfile = NULL;

    if (parse_redirections(command, &outfile, &infile, &errfile) == -1) {
        return;
    }

    int argc = tokenize_command(command, args, MAX_ARGS);
    if (argc < 0) {
        return;
    }
    if (argc == 0) {
        return;
    }

    pid_t pid = fork();

    if (pid == 0) {
        if (outfile != NULL) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "Error: Cannot open or create output file '%s'.\n", outfile);
                exit(1);
            }
            dup2(fd, 1);
            close(fd);
        }

        if (infile != NULL) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "Error: Cannot open input file '%s': File not found.\n", infile);
                exit(1);
            }
            dup2(fd, 0);
            close(fd);
        }

        if (errfile != NULL) {
            int fd = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "Error: Cannot open or create error file '%s'.\n", errfile);
                exit(1);
            }
            dup2(fd, 2);
            close(fd);
        }

        execvp(args[0], args);
        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    } else {
        wait(NULL);
    }
}

void execute_command_with_redirections(char *cmd, char *infile, char *outfile, char *errfile) {
    char *args[MAX_ARGS];

    if (infile != NULL) {
        int fd = open(infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open input file '%s': File not found.\n", infile);
            exit(1);
        }
        dup2(fd, 0);
        close(fd);
    }

    if (outfile != NULL) {
        int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open or create output file '%s'.\n", outfile);
            exit(1);
        }
        dup2(fd, 1);
        close(fd);
    }

    if (errfile != NULL) {
        int fd = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open or create error file '%s'.\n", errfile);
            exit(1);
        }
        dup2(fd, 2);
        close(fd);
    }

    int argc = tokenize_command(cmd, args, MAX_ARGS);
    if (argc < 0) {
        exit(1);
    }
    if (argc == 0) {
        fprintf(stderr, "Error: Empty command.\n");
        exit(1);
    }

    execvp(args[0], args);
    fprintf(stderr, "Command not found: %s\n", args[0]);
    exit(1);
}
