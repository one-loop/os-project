#include "executor.h"
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    // main function to run the shell. It prints $, thenr eads a line with fgets
    // strips the newline, handles exit, and either calls run_multi_pipe_command() 
    // if the line contains a |, or run_command() otherwise.
    
    // this fixed-size buffer holds one full command line from the user.
    // phase 1 intentionally keeps input handling simple with fgets.
    char command[256];

    // this is the main read-eval loop for the shell.
    // we keep prompting until the user exits or stdin closes.
    while (1) {
        // print a minimal unix-like prompt.
        printf("$ ");

        // read one line from stdin; if we hit eof (ctrl-d) we stop cleanly.
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        // remove trailing newline so parser/execution logic receives clean input.
        command[strcspn(command, "\n")] = '\0';

        // exit is handled as a builtin control command in the parent shell loop.
        if (strcmp(command, "exit") == 0) {
            exit(0);
        } else if (strchr(command, '|') != NULL) {
            // any command containing a pipe is delegated to pipeline logic.
            run_multi_piped_command(command);
        } else {
            // otherwise execute as a single command (with optional redirections).
            run_command(command);
        }
    }

    // returning from main indicates a normal shell shutdown.
    return 0;
}
