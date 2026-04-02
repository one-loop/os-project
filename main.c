#include "executor.h"
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char command[256];

    while (1) {
        printf("$ ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "exit") == 0) {
            exit(0);
        } else if (strchr(command, '|') != NULL) {
            run_multi_piped_command(command);
        } else {
            run_command(command);
        }
    }

    return 0;
}
