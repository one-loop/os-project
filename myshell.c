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

int parse_redirections(char *command, char **outfile, char **infile, char **errfile) { // Returns 0 on success, -1 on error
    // check for error redirection
    char *err_redir = strstr(command, "2>");
    if (err_redir != NULL) {
        *err_redir = '\0'; // split command from file
        err_redir += 2; // move past "2>"
        while (*err_redir == ' ') err_redir++; // trim leading spaces
        // trim trailing spaces/newlines
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

    // check for output redirection (>) in the input string
    char *out_redir = strchr(command, '>');
    if (out_redir != NULL) {
        *out_redir = '\0'; // split command from file
        out_redir++; // move past >
        // remove leading spaces
        while (*out_redir == ' ') out_redir++;
        // trim trailing spaces/newlines
        char *end = out_redir + strlen(out_redir) - 1;
        while (end > out_redir && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        if (strlen(out_redir) == 0) {
            fprintf(stderr, "Output file not specified.\n");
            return -1;
        }
        *outfile = out_redir; // file name to send output to
    }

    // input redirection <
    char *in_redir = strchr(command, '<');
    if (in_redir != NULL) {
        *in_redir = '\0'; // split command from file
        in_redir++; // move past < character
        // remove leading spaces
        while (*in_redir == ' ') in_redir++;
        // trim trailing spaces/newlines
        char *end = in_redir + strlen(in_redir) - 1;
        while (end > in_redir && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        if (strlen(in_redir) == 0) {
            fprintf(stderr, "Input file not specified.\n");
            return -1;
        }
        *infile = in_redir; // set file name to take input from
    }
    return 0; // Success
}

void run_command(char *command) {
    char *args[10]; // create an array to store the arguments
    int i = 0;

    char *outfile = NULL;
    char *infile = NULL;
    char *errfile = NULL;

    // Parse all redirections
    if (parse_redirections(command, &outfile, &infile, &errfile) == -1) {
        return; // Error already printed
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
                fprintf(stderr, "Error: Cannot open input file '%s': File not found.\n", infile);
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

        fprintf(stderr, "Command not found: %s\n", args[0]); // only runs if exec fails
        exit(1);
    } else {
        // running in parent process
        wait(NULL); // parent waits for child to finish
        // printf("Child finished.\n");
    }
}

// Helper function to execute a single command with redirections
void execute_command_with_redirections(char *cmd, char *infile, char *outfile, char *errfile) {
    char *args[10];
    
    // Handle input redirection if present
    if (infile != NULL) {
        int fd = open(infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Error: Cannot open input file '%s': File not found.\n", infile);
            exit(1);
        }
        dup2(fd, 0);
        close(fd);
    }
    
    // Handle output redirection if present
    if (outfile != NULL) {
        int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("open");
            exit(1);
        }
        dup2(fd, 1);
        close(fd);
    }
    
    // Handle error redirection if present
    if (errfile != NULL) {
        int fd = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("open");
            exit(1);
        }
        dup2(fd, 2);
        close(fd);
    }
    
    // Parse cmd into args
    int i = 0;
    char *token = strtok(cmd, " ");
    while (token != NULL && i < 10 - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;
    
    // Execute command
    execvp(args[0], args);
    fprintf(stderr, "Command not found: %s\n", args[0]);
    exit(1);
}

// New function to handle multiple piped commands (e.g., cmd1 | cmd2 | cmd3 | ...)
void run_multi_piped_command(char *command) {
    // First, count how many commands we have (this will be equal to count pipes + 1)
    int num_commands = 1;
    char *temp = command;
    while ((temp = strchr(temp, '|')) != NULL) {
        num_commands++;
        temp++;
    }
    
    // Split commands by pipe symbol
    char *commands[num_commands];
    int cmd_idx = 0;
    char *token = strtok(command, "|");
    while (token != NULL && cmd_idx < num_commands) {
        // Trim leading spaces
        while (*token == ' ') token++;
        // Trim trailing spaces
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        // Check if command is empty
        if (strlen(token) == 0) {
            fprintf(stderr, "Empty command between pipes.\n");
            return;
        }
        commands[cmd_idx++] = token;
        token = strtok(NULL, "|");
    }
    
    // Check if last command is missing (e.g., "cmd1 |")
    if (cmd_idx < num_commands) {
        fprintf(stderr, "Command missing after pipe.\n");
        return;
    }
    
    // We need (num_commands - 1) pipes to connect num_commands commands
    int num_pipes = num_commands - 1;
    int pipes[num_pipes][2]; // pipes[i][0] = read end, pipes[i][1] = write end
    
    // Create all pipes
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return;
        }
    }
    
    // Array to store all child PIDs
    pid_t pids[num_commands];
    
    // Fork and execute each command
    for (int i = 0; i < num_commands; i++) {
        pids[i] = fork();
        
        if (pids[i] == 0) {
            // CHILD PROCESS
            
            // If not the first command, redirect stdin from previous pipe
            if (i > 0) {
                dup2(pipes[i-1][0], 0); // stdin comes from previous pipe's read end
            }
            
            // If not the last command, redirect stdout to next pipe
            if (i < num_commands - 1) {
                dup2(pipes[i][1], 1); // stdout goes to current pipe's write end
            }
            
            // Close all pipe file descriptors in child
            for (int j = 0; j < num_pipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            // Parse redirections for this command
            char *outfile = NULL, *infile = NULL, *errfile = NULL;
            if (parse_redirections(commands[i], &outfile, &infile, &errfile) == -1) {
                exit(1); // Error in parsing redirections
            }
            
            // Execute the command with its redirections
            execute_command_with_redirections(commands[i], infile, outfile, errfile);
            
            // Should never reach here if exec succeeds (inshallah), but if it does, exit with error
            exit(1);
        }
    }
    
    // PARENT PROCESS
    // Close all pipe file descriptors in parent
    for (int i = 0; i < num_pipes; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children to finish
    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

// Old function to handle piped commands (e.g., cmd1 | cmd2) - KEEPING FOR REFERENCE BUT NOT USED
void run_piped_command(char *command) {
    // Find the pipe symbol
    char *pipe_pos = strchr(command, '|');
    if (pipe_pos == NULL) {
        // No pipe found, shouldn't happen but we need to handle
        run_command(command);
        return;
    }

    // Split command into two parts: before and after pipe
    *pipe_pos = '\0'; // Replace | with null terminator
    char *cmd1 = command; // First command (before pipe)
    char *cmd2 = pipe_pos + 1; // Second command (after pipe)

    // Trim leading/trailing spaces from both commands
    while (*cmd1 == ' ') cmd1++;
    while (*cmd2 == ' ') cmd2++;

    // Create the pipe (pipefd[0] is read end, pipefd[1] is write end)
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    // Fork first child for cmd1
    pid_t pid1 = fork();
    if (pid1 == 0) {
        // CHILD 1: Execute cmd1 and send output to pipe
        
        // Redirect stdout to pipe write end
        dup2(pipefd[1], 1); // stdout now goes to pipe
        
        // Close both pipe file descriptors (already duplicated)
        close(pipefd[0]);
        close(pipefd[1]);

        // Parse and execute cmd1 (it might have redirections)
        char *args[10];
        char *outfile = NULL, *infile = NULL, *errfile = NULL;
        
        // Parse redirections for cmd1
        parse_redirections(cmd1, &outfile, &infile, &errfile);
        
        // Handle input redirection if present
        if (infile != NULL) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, 0);
            close(fd);
        }
        
        // Handle error redirection if present
        if (errfile != NULL) {
            int fd = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, 2);
            close(fd);
        }
        
        // Parse cmd1 into args
        int i = 0;
        char *token = strtok(cmd1, " ");
        while (token != NULL && i < 10 - 1) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;
        
        // Finally, execute cmd1
        execvp(args[0], args);
        perror("command failed");
        exit(1);
    }

    // Fork second child for cmd2
    pid_t pid2 = fork();
    if (pid2 == 0) {
        // CHILD 2: Execute cmd2 and receive input from pipe
        
        // Redirect stdin to pipe read end
        dup2(pipefd[0], 0); // stdin now comes from pipe
        
        // Close both pipe file descriptors (already duplicated)
        close(pipefd[0]);
        close(pipefd[1]);

        // Now parse and execute cmd2 (it might have redirections)
        char *args[10];
        char *outfile = NULL, *infile = NULL, *errfile = NULL;
        
        // Parse redirections for cmd2
        parse_redirections(cmd2, &outfile, &infile, &errfile);
        
        // Handle output redirection if present
        if (outfile != NULL) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, 1);
            close(fd);
        }
        
        // Handle error redirection if present
        if (errfile != NULL) {
            int fd = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, 2);
            close(fd);
        }
        
        // Parse cmd2 into args
        int i = 0;
        char *token = strtok(cmd2, " ");
        while (token != NULL && i < 10 - 1) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;
        
        // Finally, execute cmd2
        execvp(args[0], args);
        perror("command failed");
        exit(1);
    }

    // PARENT PROCESS
    // Close both pipe ends in parent (children have their own copies)
    close(pipefd[0]);
    close(pipefd[1]);
    
    // Wait for both children to finish
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
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
        } else if (strchr(command, '|') != NULL) {
            // Command contains pipe(s), use multi-pipe execution
            run_multi_piped_command(command);
        } else {
            // Regular command without pipes
            run_command(command);
            // shell continues
        }
    }

    return 0;
}
