#include "builtins.h"

#include <stdio.h>
#include <string.h>

// Minimal builtin support for Phase 1: handle `echo` so flags like `-e`
// are not printed literally on systems where `/bin/echo` doesn't interpret them.
int run_builtin(char **args) {
    if (args == NULL || args[0] == NULL) {
        return 0;
    }

    if (strcmp(args[0], "echo") != 0) {
        return 0;
    }

    int newline = 1;
    int i = 1;

    // Support common `echo` flags used in testing.
    // -n: do not print trailing newline
    if (args[i] != NULL && strcmp(args[i], "-n") == 0) {
        newline = 0;
        i++;
    }

    // -e: in this project we rely on our tokenizer to decode escapes inside quotes.
    // The key fix for your mark-losing case is that `-e` should not be printed.
    if (args[i] != NULL && strcmp(args[i], "-e") == 0) {
        i++;
    }

    for (; args[i] != NULL; i++) {
        if (i > 1) {
            fputc(' ', stdout);
        }
        fputs(args[i], stdout);
    }

    if (newline) {
        fputc('\n', stdout);
    }

    return 1;
}

