#ifndef EXECUTOR_H
#define EXECUTOR_H

/* Run a single command (no pipes): fork, apply redirections, execvp. */
void run_command(char *command);

/* Used by pipeline children: apply in/out/err files then execvp(cmd). */
void execute_command_with_redirections(char *cmd, char *infile, char *outfile, char *errfile);

#endif
