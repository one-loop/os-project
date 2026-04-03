#include "executor.h"
#include "parser.h"
#include "builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_ARGS 64

void run_command(char *command) {
    // args becomes the argv array passed into execvp.
    char *args[MAX_ARGS];
    // these pointers are set by parse_redirections when operators are present.
    char *outfile = NULL;
    char *infile = NULL;
    char *errfile = NULL;

    // split command from any <, >, 2> operators and extract target files.
    if (parse_redirections(command, &outfile, &infile, &errfile) == -1) {
        return;
    }

    // tokenize remaining command text into program + arguments.
    int argc = tokenize_command(command, args, MAX_ARGS);
    // parser already printed errors for invalid quoting.
    if (argc < 0) {
        return;
    }
    // empty command after parsing should be ignored safely.
    if (argc == 0) {
        return;
    }

    // create one child for command execution.
    pid_t pid = fork();

    if (pid == 0) {
        // child process: apply requested redirections before execution.
        if (outfile != NULL) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "Error: Cannot open or create output file '%s'.\n", outfile);
                exit(1);
            }
            // route stdout to output file.
            dup2(fd, 1);
            close(fd);
        }

        if (infile != NULL) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "Error: Cannot open input file '%s': File not found.\n", infile);
                exit(1);
            }
            // route stdin from input file.
            dup2(fd, 0);
            close(fd);
        }

        if (errfile != NULL) {
            int fd = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "Error: Cannot open or create error file '%s'.\n", errfile);
                exit(1);
            }
            // route stderr to error file.
            dup2(fd, 2);
            close(fd);
        }

        // run builtins in the child so redirections affect builtin output too.
        if (run_builtin(args)) {
            exit(0);
        }

        // replace child image with external command.
        execvp(args[0], args);
        // if execvp returns, command lookup/execution failed.
        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    } else {
        // parent waits so prompt appears after this command finishes.
        wait(NULL);
    }
}

void execute_command_with_redirections(char *cmd, char *infile, char *outfile, char *errfile) {
    // this helper is used by pipeline children so each stage can apply its own
    // local redirections before exec.
    char *args[MAX_ARGS];

    if (infile != NULL) {
        int fd = open(infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open input file '%s': File not found.\n", infile);
            exit(1);
        }
        // override current stdin for this process.
        dup2(fd, 0);
        close(fd);
    }

    if (outfile != NULL) {
        int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open or create output file '%s'.\n", outfile);
            exit(1);
        }
        // override current stdout for this process.
        dup2(fd, 1);
        close(fd);
    }

    if (errfile != NULL) {
        int fd = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open or create error file '%s'.\n", errfile);
            exit(1);
        }
        // override current stderr for this process.
        dup2(fd, 2);
        close(fd);
    }

    // parse command text for this stage into argv.
    int argc = tokenize_command(cmd, args, MAX_ARGS);
    if (argc < 0) {
        exit(1);
    }
    if (argc == 0) {
        fprintf(stderr, "Error: Empty command.\n");
        exit(1);
    }

    // run builtins here so commands like `echo -e ... | ...` work consistently.
    if (run_builtin(args)) {
        exit(0);
    }

    // execute external command for this pipeline stage.
    execvp(args[0], args);
    fprintf(stderr, "Command not found: %s\n", args[0]);
    exit(1);
}
