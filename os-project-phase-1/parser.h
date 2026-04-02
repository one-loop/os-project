#ifndef PARSER_H
#define PARSER_H

/* Parse 2>, >, and < from command; null-terminates command at operators; sets *outfile, *infile, *errfile.
 * Returns 0 on success, -1 on error (message printed to stderr). */
int parse_redirections(char *command, char **outfile, char **infile, char **errfile);

/* Tokenize command line; preserves quoted "..." and '...' as one argument.
 * Decodes \\n, \\t, etc. inside double quotes.
 * Returns argument count, or -1 on unmatched quote (error printed). */
int tokenize_command(char *command, char **args, int max_args);

#endif
