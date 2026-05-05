#include "builtins.h"

#include <stdio.h>
#include <string.h>

// builtin dispatcher for commands we choose to handle inside myshell.
// returning 1 means "handled here", returning 0 means "let execvp handle it".
int run_builtin(char **args) {
    // function to run builtin commands like echo, cd, pwd, etc.
    // handles -n and -e flags for echo, and cd for change directory
    // no argv means no builtin work to do.
    if (args == NULL || args[0] == NULL) {
        return 0;
    }

    // currently we only support echo as an internal builtin.
    if (strcmp(args[0], "echo") != 0) {
        return 0;
    }

    // echo prints a trailing newline by default.
    int newline = 1;
    int i = 1;

    // support -n so output can omit final newline when requested.
    if (args[i] != NULL && strcmp(args[i], "-n") == 0) {
        newline = 0;
        i++;
    }

    // support -e by consuming it here; escape decoding is already handled in parser
    // for double-quoted strings, so we avoid printing "-e" literally.
    if (args[i] != NULL && strcmp(args[i], "-e") == 0) {
        i++;
    }

    // print remaining arguments separated by spaces.
    for (; args[i] != NULL; i++) {
        if (i > 1) {
            fputc(' ', stdout);
        }
        fputs(args[i], stdout);
    }

    // add final newline unless -n was provided.
    if (newline) {
        fputc('\n', stdout);
    }

    // handled by builtin implementation.
    return 1;
}

