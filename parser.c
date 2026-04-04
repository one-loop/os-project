#include "parser.h"

#include <stdio.h>
#include <string.h>

static void unescape_in_place(char *s) {
    // src reads original characters; dst writes decoded characters back
    // into the same buffer to avoid extra allocations.
    char *src = s;
    char *dst = s;

    while (*src != '\0') {
        // handle backslash escapes when there is a following character.
        if (*src == '\\' && *(src + 1) != '\0') {
            src++;
            // map common escape codes used in tests and quoted strings.
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
                // unknown escape: keep literal character after backslash.
                *dst++ = *src;
            }
            src++;
            continue;
        }

        // normal non-escaped character.
        *dst++ = *src++;
    }

    // terminate decoded string.
    *dst = '\0';
}

// function to tokenize the command string into arguments
// supports single and double quotes, and escapes inside double quotes
int tokenize_command(char *command, char **args, int max_args) {
    // argc is how many tokens we produced so far.
    int argc = 0;
    // p walks through the mutable command buffer.
    char *p = command;

    while (*p != '\0') {
        // skip leading whitespace between tokens.
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        // reserve one slot for trailing NULL because execvp needs argv to be null-terminated.
        if (argc >= max_args - 1) break;

        if (*p == '"' || *p == '\'') {
            // quoted token: keep spaces inside the token.
            char quote = *p++;
            args[argc++] = p;
            while (*p != '\0' && *p != quote) p++;
            if (*p == quote) {
                // terminate token at closing quote.
                *p = '\0';
                if (quote == '"') {
                    // only decode escapes inside double quotes.
                    unescape_in_place(args[argc - 1]);
                }
                p++;
            } else {
                // unmatched quote is a parsing error.
                fprintf(stderr, "Error: Unmatched quote in command.\n");
                return -1;
            }
        } else {
            // unquoted token: split on space/tab.
            args[argc++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') p++;
            if (*p != '\0') {
                *p = '\0';
                p++; // move to the next character
            }
        }
    }

    // execvp-compatible argv sentinel.
    args[argc] = NULL;
    return argc;
}

int parse_redirections(char *command, char **outfile, char **infile, char **errfile) {
    // fuhnction to find 2>, > and <, splits the command string in place, and fills pointers
    // to file names for stdout, stdin and stderr

    // parse stderr redirection first so "2>" is not mistaken for plain ">".
    char *err_redir = strstr(command, "2>");
    if (err_redir != NULL) {
        // split command and target filename in-place.
        *err_redir = '\0';
        err_redir += 2;
        while (*err_redir == ' ') err_redir++; // skip leading spaces

        // trim trailing spaces/newlines from filename.
        char *end = err_redir + strlen(err_redir) - 1;
        while (end > err_redir && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }

        // reject missing filename after operator.
        if (strlen(err_redir) == 0) {
            fprintf(stderr, "Error output file not specified.\n");
            return -1;
        }
        *errfile = err_redir;
    }

    // parse stdout redirection.
    char *out_redir = strchr(command, '>');
    // if there is a >, then we need to split the command string in place and 
    // fill the pointer to the file name for stdout
    if (out_redir != NULL) {
        *out_redir = '\0'; // null terminate the command string at the > operator
        out_redir++;
        while (*out_redir == ' ') out_redir++; // skip leading spaces
        char *end = out_redir + strlen(out_redir) - 1;
        while (end > out_redir && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0'; // null terminate the filename at the end of the string
            end--; // trim trailing spaces/newlines from filename
        }
        if (strlen(out_redir) == 0) {
            // if the filename is empty, then we need to print an error message
            fprintf(stderr, "Output file not specified.\n");
            return -1;
        }
        *outfile = out_redir; // set the pointer to the file name for stdout
    }

    // parse stdin redirection.
    char *in_redir = strchr(command, '<');
    // if there is a <, then we need to split the command string in place and 
    // fill the pointer to the file name for stdin
    if (in_redir != NULL) {
        *in_redir = '\0'; // null terminate the command string at the < operator
        in_redir++;
        while (*in_redir == ' ') in_redir++; // skip leading spaces
        char *end = in_redir + strlen(in_redir) - 1;
        while (end > in_redir && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0'; // null terminate the filename at the end of the string
            end--; // trim trailing spaces/newlines from filename
        }
        if (strlen(in_redir) == 0) {
            // if the filename is empty, then we need to print an error message
            fprintf(stderr, "Input file not specified.\n");
            return -1; // return -1 to indicate an error
        }
        *infile = in_redir; // set the pointer to the file name for stdin
    }

    // success means redirection pointers (if any) are ready for the executor.
    return 0;
}
