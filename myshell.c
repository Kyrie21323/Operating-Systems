#include <stdio.h>    
#include <stdlib.h>   
#include <string.h>   
#include <unistd.h>   
#include <sys/wait.h> 
#include <fcntl.h>    

#define MAX_CMD_LENGTH 1024 
#define MAX_ARGS 64         

//function to parse command line
void parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile) {
    int i = 0;
    //split cmd string with strtok
    char *token = strtok(cmd, " \t\n");

    while (token != NULL) {
        //check for redirection symbols
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t\n"); //next part is filename
            if (token == NULL) {
                printf("Input file not specified.\n");
                args[0] = NULL;
                return;
            }
            *inputFile = token;
            token = strtok(NULL, " \t\n");
            continue;                      //skip adding filename to args
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t\n"); //next part is filename
            if (token == NULL) {
                printf("Output file not specified.\n");
                args[0] = NULL;
                return;
            }
            *outputFile = token;
            token = strtok(NULL, " \t\n");
            continue;                      //skip adding filename to args
        } else if (strcmp(token, "2>") == 0) {
            token = strtok(NULL, " \t\n"); //next part is filename
            if (token == NULL) {
                printf("Error output file not specified.\n");
                args[0] = NULL;
                return;
            }
            *errorFile = token;
            token = strtok(NULL, " \t\n");
            continue;                      //skip adding filename to args
        } else {
            args[i++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    //execvp needs null at end
    args[i] = NULL;
}

//function to run command
void execute_command(char *args[], char *inputFile, char *outputFile, char *errorFile) {
    //make new process
    pid_t pid = fork();

    if (pid < 0) {
        //if fork didnt work
        perror("fork failed");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        //in child process now

        //handle input redirection <
        if (inputFile != NULL) {
            int fd_in = open(inputFile, O_RDONLY);
            if (fd_in < 0) {
                printf("File not found: %s\n", inputFile);
                exit(EXIT_FAILURE);
            }
            //redirect stdin to file
            dup2(fd_in, STDIN_FILENO);
            close(fd_in); //close original fd
        }

        //handle output redirection >
        if (outputFile != NULL) {
            //create file with permissions
            int fd_out = creat(outputFile, 0644);
            if (fd_out < 0) {
                perror("bad output file");
                exit(EXIT_FAILURE);
            }
            //redirect stdout to file
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        //handle error redirection 2>
        if (errorFile != NULL) {
            int fd_err = creat(errorFile, 0644);
            if (fd_err < 0) {
                perror("bad error file");
                exit(EXIT_FAILURE);
            }
            //redirect stderr to file
            dup2(fd_err, STDERR_FILENO);
            close(fd_err);
        }

        //run the command
        if (execvp(args[0], args) < 0) {
            //execvp failed, something is wrong
            printf("Command not found.\n");
            perror("exec failed");
            exit(EXIT_FAILURE);
        }
    } else {
        //parent waits for child to finish
        wait(NULL);
    }
}


int main() {
    char cmd[MAX_CMD_LENGTH];      //holds typed command
    char *args[MAX_ARGS];          //holds parsed args

    //shell loop, runs forever
    while (1) {
        // 1.show prompt
        printf("$ ");
        fflush(stdout); //show prompt right away

        // 2.read command
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            //handles ctrl+d
            break;
        }

        // 3.check for "exit"
        if (strcmp(cmd, "exit\n") == 0) {
            break; //leave loop
        }

        // 4.parse it
        char *inputFile = NULL;
        char *outputFile = NULL;
        char *errorFile = NULL;
        parse_command(cmd, args, &inputFile, &outputFile, &errorFile);

        // 5.run it if not empty
        if (args[0] != NULL) {
            execute_command(args, inputFile, outputFile, errorFile);
        }
    }

    return 0;
}

