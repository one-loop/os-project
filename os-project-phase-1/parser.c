#include "parser.h"

#include <stdio.h>
#include <string.h>

static void unescape_in_place(char *s) {
    char *src = s;
    char *dst = s;

    while (*src != '\0') {
        if (*src == '\\' && *(src + 1) != '\0') {
            src++;
            if (*src == 'n') {
                *dst++ = '\n';
            } else if (*src == 't') {
                *dst++ = '\t';
            } else if (*src == 'r') {
                *dst++ = '\r';
            } else if (*src == '\\') {
                *dst++ = '\\';
            } else if (*src == '"') {
                *dst++ = '"';
            } else {
                *dst++ = *src;
            }
            src++;
            continue;
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

int tokenize_command(char *command, char **args, int max_args) {
    int argc = 0;
    char *p = command;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (argc >= max_args - 1) break;

        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            args[argc++] = p;
            while (*p != '\0' && *p != quote) p++;
            if (*p == quote) {
                *p = '\0';
                if (quote == '"') {
                    unescape_in_place(args[argc - 1]);
                }
                p++;
            } else {
                fprintf(stderr, "Error: Unmatched quote in command.\n");
                return -1;
            }
        } else {
            args[argc++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') p++;
            if (*p != '\0') {
                *p = '\0';
                p++;
            }
        }
    }

    args[argc] = NULL;
    return argc;
}

int parse_redirections(char *command, char **outfile, char **infile, char **errfile) {
    char *err_redir = strstr(command, "2>");
    if (err_redir != NULL) {
        *err_redir = '\0';
        err_redir += 2;
        while (*err_redir == ' ') err_redir++;
        char *end = err_redir + strlen(err_redir) - 1;
        while (end > err_redir && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        if (strlen(err_redir) == 0) {
            fprintf(stderr, "Error output file not specified.\n");
            return -1;
        }
        *errfile = err_redir;
    }

    char *out_redir = strchr(command, '>');
    if (out_redir != NULL) {
        *out_redir = '\0';
        out_redir++;
        while (*out_redir == ' ') out_redir++;
        char *end = out_redir + strlen(out_redir) - 1;
        while (end > out_redir && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        if (strlen(out_redir) == 0) {
            fprintf(stderr, "Output file not specified.\n");
            return -1;
        }
        *outfile = out_redir;
    }

    char *in_redir = strchr(command, '<');
    if (in_redir != NULL) {
        *in_redir = '\0';
        in_redir++;
        while (*in_redir == ' ') in_redir++;
        char *end = in_redir + strlen(in_redir) - 1;
        while (end > in_redir && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        if (strlen(in_redir) == 0) {
            fprintf(stderr, "Input file not specified.\n");
            return -1;
        }
        *infile = in_redir;
    }
    return 0;
}
