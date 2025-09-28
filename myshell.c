#include <stdio.h>    
#include <stdlib.h>   
#include <string.h>   
#include <unistd.h>   
#include <sys/wait.h> 
#include <fcntl.h>    

#define MAX_CMD_LENGTH 1024 
#define MAX_ARGS 64
#define MAX_PIPES 10         

//function declarations
void parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile);

//function to validate pipeline syntax
int validate_pipeline(char *cmd){
    //check for leading pipe
    while(*cmd == ' ' || *cmd == '\t' || *cmd == '\n'){
        cmd++;
    }
    if(*cmd == '|'){
        printf("Command missing after pipe.\n");
        return -1;
    }
    
    //check for trailing pipe
    int len = strlen(cmd);
    while(len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\t' || cmd[len-1] == '\n')){
        len--;
    }
    if(len > 0 && cmd[len-1] == '|'){
        printf("Command missing after pipe.\n");
        return -1;
    }
    
    //check for empty stages (|| or | |)
    char *ptr = cmd;
    while(*ptr){
        if(*ptr == '|'){
            ptr++;
            //skip whitespace after |
            while(*ptr == ' ' || *ptr == '\t' || *ptr == '\n'){
                ptr++;
            }
            //if we hit another | or end of string, it's empty
            if(*ptr == '|' || *ptr == '\0'){
                printf("Empty command between pipes.\n");
                return -1;
            }
        }else{
            ptr++;
        }
    }
    
    return 0;
}

//structure to hold stage information
typedef struct{
    char *args[MAX_ARGS];
    char *inputFile;
    char *outputFile;
    char *errorFile;
}
Stage;

//function to execute piped commands
void execute_pipeline(char *cmd){
    //validate pipeline syntax first
    if(validate_pipeline(cmd) != 0){
        return;
    }
    
    //split into stages
    Stage stages[MAX_PIPES];
    int numStages = 0;
    
    char *saveptr;  //for strtok_r
    char *stage_cmd = strtok_r(cmd, "|", &saveptr);
    while(stage_cmd != NULL && numStages < MAX_PIPES){
        //trim whitespace
        while(*stage_cmd == ' ' || *stage_cmd == '\t' || *stage_cmd == '\n'){
            stage_cmd++;
        }
        
        //parse this stage using existing parser
        char *args[MAX_ARGS];
        char *inputFile = NULL;
        char *outputFile = NULL;
        char *errorFile = NULL;
        
        parse_command(stage_cmd, args, &inputFile, &outputFile, &errorFile);
        
        //check if parsing failed (args[0] is NULL)
        if(args[0] == NULL){
            return;                                     //don't execute pipeline
        }
        
        //copy to stage structure
        int k = 0;
        for(int i = 0; args[i] != NULL; i++){
            stages[numStages].args[k++] = args[i];
        }
        stages[numStages].args[k] = NULL;               //null-terminate immediately after last arg
        stages[numStages].inputFile = inputFile;
        stages[numStages].outputFile = outputFile;
        stages[numStages].errorFile = errorFile;
        
        numStages++;
        stage_cmd = strtok_r(NULL, "|", &saveptr);
    }
    
    if(numStages == 0){
        return;
    }
    
    //create pipes
    int pipes[MAX_PIPES][2];
    for(int i = 0; i < numStages - 1; i++){
        if(pipe(pipes[i]) < 0){
            perror("pipe failed");
            return;
        }
    }
    
    //fork children
    pid_t pids[MAX_PIPES];
    for(int i = 0; i < numStages; i++){
        pids[i] = fork();
        
        if(pids[i] < 0){
            perror("fork failed");
            return;
        }else if(pids[i] == 0){                 //child process
            //handle input redirection
            if(stages[i].inputFile != NULL){
                int fd_in = open(stages[i].inputFile, O_RDONLY);
                if(fd_in < 0){
                    printf("File not found: %s\n", stages[i].inputFile);
                    exit(EXIT_FAILURE);
                }
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            }else if(i > 0){
                //connect to previous pipe
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            
            //handle output redirection
            if(stages[i].outputFile != NULL){
                int fd_out = creat(stages[i].outputFile, 0644);
                if(fd_out < 0){
                    perror("bad output file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }else if(i < numStages - 1){
                //connect to next pipe
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            //handle error redirection
            if(stages[i].errorFile != NULL){
                int fd_err = creat(stages[i].errorFile, 0644);
                if(fd_err < 0){
                    perror("bad error file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd_err, STDERR_FILENO);
                close(fd_err);
            }
            
            //close all pipe file descriptors
            for(int j = 0; j < numStages - 1; j++)  {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            //execute the command
            if(execvp(stages[i].args[0], stages[i].args) < 0)   {
                printf("Command not found.\n");
                perror("exec failed");
                exit(EXIT_FAILURE);
            }
        }
    }
    
    //parent process - close all pipe file descriptors
    for(int i = 0; i < numStages - 1; i++){
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    //wait for the last process (minimum requirement)
    waitpid(pids[numStages-1], NULL, 0);
    
    //optional: wait for all children to avoid zombies
    for(int i = 0; i < numStages; i++){
        if(i != numStages-1){                       //don't wait twice for last process
            waitpid(pids[i], NULL, 0);
        }
    }
}

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
        
        //check if command contains pipes
        if(strchr(cmd, '|') != NULL){
            //execute as pipeline
            execute_pipeline(cmd);
        }else{
            //execute as single command
            parse_command(cmd, args, &inputFile, &outputFile, &errorFile);

            // 5.run it if not empty
            if (args[0] != NULL) {
                execute_command(args, inputFile, outputFile, errorFile);
            }
        }
    }

    return 0;
}