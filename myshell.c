#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> // fork, execvp
#include <sys/wait.h> // wait
#include <fcntl.h> // for open() 

void print_help() {
    printf("Available commands: \n");
    printf("    help            - show this help message\n");
    printf("    ls              - list files in current directory\n");
    printf("    ls -l           - list \n");
    printf("    exit            - exit the program\n");
}

void run_command(char *command) {
    char *args[10]; // create an array to store the arguments
    int i = 0;

    char *outfile = NULL;
    char *infile = NULL;
    char *errfile = NULL;

    // check for error redirection
    char *err_redir = strstr(command, "2>");
    if (err_redir != NULL) {
        *err_redir = '\0'; // split command from file
        err_redir += 2; // move past "2>"
        while (*err_redir == ' ') err_redir++; // trim spaces
        errfile = err_redir;
    }

    // check for output redirection (>) in the input string
    char *out_redir = strchr(command, '>');
    if (out_redir != NULL) {
        *out_redir = '\0'; // split command from file
        out_redir++; // move past >
        // remove leading spaces
        while (*out_redir == ' ') out_redir++;
        outfile = out_redir; // file name to send output to
    }

    // input redirection <
    char *in_redir = strchr(command, '<');
    if (in_redir != NULL) {
        *in_redir = '\0'; // split command from file
        in_redir++; // move past < character
        // remove leading spaces
        while (*in_redir == ' ') in_redir++;
        infile = in_redir; // set file name to take input from
    }
    

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
        // if output redirection exists, open file and dup2
        if (outfile != NULL) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, 1); // replace stdout with the file
            close(fd); // fd no longer needed
        }

        // input redirection
        if (infile != NULL) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, 0); // replace stdin with the file 
            close(fd);
        }

        // error redirection
        if (errfile != NULL) {
            int fd = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, 2); // redirect stderr
            close(fd);
        }

        // e.g. child will call execvp("ls", {"ls", "-l", NULL});
        execvp(args[0], args); // child becomes /bin/ls

        perror("command failed"); // only runs if exec fails
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
