#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> // fork, execvp
#include <sys/wait.h> // wait

void print_help() {
    printf("Available commands: \n");
    printf("    help            - show this help message\n");
    printf("    ls              - list files in current directory\n");
    printf("    ls -l           - list \n");
    printf("    exit            - exit the program\n");
}

void list_directory() {
    // 
}

void run_command(char *command) {
    char *args[10]; // create an array to store the arguments
    int i = 0;

    // printf("Running command %s\n", command);

    // tokenize the input command (split by spaces to separate arguments)
    char *token = strtok(command, " ");
    while (token != NULL && i < 10 - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    // The args array should end in NULL (e.g. {"ls", "-l", NULL})
    args[i] = NULL; // very important

    pid_t pid = fork();

    if (pid == 0) {
        // running in child process
        // printf("Running command in child process.\n");
        // e.g. child will call execvp("ls", {"ls", "-l", NULL});
        execvp(args[0], args); // child becomes /bin/ls

        // only runs if exec fails
        perror("command failed");
        exit(1);
    } else {
        // running in parent process
        wait(NULL); // parent waits for child to finish
        // printf("Child finished.\n");
    }
}


int main() {
    char command[256]; // string to store user's command

    while (1) {
        // user shell
        printf("$ ");
        fgets(command, sizeof(command), stdin); // get the command from standard input

        // Remove newline
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "exit") == 0) {
            // printf("Exiting...\n");
            exit(0); // exit(0); exits the entire program immediately
            // return 0; (exits only from main)
        } else if (strcmp(command, "help") == 0) {
            print_help();
        } else {
            run_command(command);
            // shell continues
        }
    }

    return 0;
}