#include "exec.h"
#include "parse.h"
#include "redir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

//maximum length for command input buffer
#define MAX_CMD_LENGTH 1024 
//maximum number of arguments a command can have
#define MAX_ARGS 64         
//maximum number of pipes in a pipeline
#define MAX_PIPES 10

//structure definition for pipeline stages
//each stage in a pipeline is a separate command with its own arguments and redirections
typedef struct {
    char *args[MAX_ARGS];
    char *inputFile;
    char *outputFile;
    char *errorFile;
} Stage;

/*utility function to skip leading whitespace characters.
This function advances the pointer past any spaces, tabs, or newlines at the beginning of a string, returning a pointer to the first non-whitespace character
*/
static char* skip_whitespace(char *str){
    //loop through string while current character is whitespace
    while(*str == ' ' || *str == '\t' || *str == '\n'){
        str++;
    }
    return str;
}

/*Execute a single command with optional file redirections
This function creates a child process to run the command and handles redirections. The parent process waits for the child to complete before returning
*/
void execute_command(char *args[], char *inputFile, char *outputFile, char *errorFile) {
    //create a child process using fork()
    pid_t pid = fork();

    if(pid < 0){
        perror("fork failed");
        return;
    }
    
    if(pid == 0){
        //child process : execute the command with redirections, set up input redirection if specified, redirect stdin to read from inputFile
        if(inputFile && setup_redirection(inputFile, O_RDONLY, STDIN_FILENO) < 0){
            exit(EXIT_FAILURE);
        }
        
        //set up output redirection if specified
        //redirect stdout to write to outputFile
        if(outputFile && setup_redirection(outputFile, O_WRONLY|O_CREAT|O_TRUNC, STDOUT_FILENO) < 0){
            exit(EXIT_FAILURE);
        }
        
        //set up error redirection if specified
        //redirect stderr to write to errorFile
        if(errorFile && setup_redirection(errorFile, O_WRONLY|O_CREAT|O_TRUNC, STDERR_FILENO) < 0){
            exit(EXIT_FAILURE);
        }
        
        //execute the command using execvp, it searches PATH environment variable for the executable
        if(execvp(args[0], args) < 0){
            printf("Command not found.\n");
            exit(EXIT_FAILURE);
        }
    }else{
        //parent process : wait for child to complete, wait() blocks until child process terminates
        //this ensures the shell waits for command completion before showing next prompt
        wait(NULL);
    }
}

/*main pipeline execution function that handles commands with pipes (|)
this function creates multiple processes and connects their input/output streams using pipe() and dup2() system calls to simulate shell pipeline behavior
*/
void execute_pipeline(char *cmd){
    //validate that the pipeline syntax is correct
    if(validate_pipeline(cmd) != 0){
        return;
    }
    
    //array to store information about each stage in the pipeline
    Stage stages[MAX_PIPES];
    int numStages = 0;                  //counter for number of stages found
    int hasErrors = 0;                  //flag to track if any stage had parsing errors
    
    //split the command by pipe characters using strtok_r
    char *saveptr;                                          //save pointer for strtok_r
    char *stage_cmd = strtok_r(cmd, "|", &saveptr);         //get first stage
    
    //parse each stage of the pipeline
    while(stage_cmd != NULL && numStages < MAX_PIPES){
        stage_cmd = skip_whitespace(stage_cmd);
        
        //local variables to hold parsed information for this stage
        char *args[MAX_ARGS];                   //command arguments array
        char *inputFile = NULL;                  //input redirection file
        char *outputFile = NULL;                //output redirection file
        char *errorFile = NULL;                 //error redirection file
        
        parse_command(stage_cmd, args, &inputFile, &outputFile, &errorFile, 1);
        
        //check if parsing failed (args[0] is NULL indicates parsing error)
        if(args[0] == NULL){
            hasErrors = 1;                  //set error flag
            numStages++;
            stage_cmd = strtok_r(NULL, "|", &saveptr);
            continue;
        }
        
        //copy to stage structure
        int k = 0;
        for(int i = 0; args[i] != NULL; i++){
            stages[numStages].args[k++] = args[i];
        }
        stages[numStages].args[k] = NULL;               //null-terminate immediately after last arg
        
        //store redirection information for this stage
        stages[numStages].inputFile = inputFile;
        stages[numStages].outputFile = outputFile;
        stages[numStages].errorFile = errorFile;
        
        numStages++;
        stage_cmd = strtok_r(NULL, "|", &saveptr);
    }
    
    //check if we have at least one valid stage to execute
    if(numStages == 0){
        return;
    }
    
    //don't execute pipeline if there were parsing errors
    if(hasErrors){
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
    
    //create a child process for each stage
    pid_t pids[MAX_PIPES];
    for(int i = 0; i < numStages; i++){
        pids[i] = fork();
        
        if(pids[i] < 0){
            perror("fork failed");
            return;
        }else if(pids[i] == 0){
            /*child process : execute this stage of the pipeline
            handle explicit file redirections first (they override pipe connections)
            */
            if(stages[i].inputFile && setup_redirection(stages[i].inputFile, O_RDONLY, STDIN_FILENO) < 0){
                exit(EXIT_FAILURE);
            }
            if(stages[i].outputFile && setup_redirection(stages[i].outputFile, O_WRONLY|O_CREAT|O_TRUNC, STDOUT_FILENO) < 0){
                exit(EXIT_FAILURE);
            }
            if(stages[i].errorFile && setup_redirection(stages[i].errorFile, O_WRONLY|O_CREAT|O_TRUNC, STDERR_FILENO) < 0){
                exit(EXIT_FAILURE);
            }
            
            //connect input from previous stage (only if no explicit input redirection)
            if(i > 0 && stages[i].inputFile == NULL){
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            //connect output to next stage (only if no explicit output redirection)
            if(i < numStages - 1 && stages[i].outputFile == NULL){
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            //close pipe file descriptors that are not being used by this stage
            for(int j = 0; j < numStages - 1; j++){
                //close read end if this stage is not reading from this pipe
                if(i == 0 || j != i-1){
                    close(pipes[j][0]);
                }
                //close write end if this stage is not writing to this pipe
                if(i == numStages-1 || j != i){
                    close(pipes[j][1]);
                }
            }
            
            //execute the command
            if(execvp(stages[i].args[0], stages[i].args) < 0){
                //if we reach here, execvp failed, and an error message is printed to stderr (not stdout) to avoid interfering with pipeline
                fprintf(stderr, "Command not found in pipe sequence.\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    
    /*parent process : close all pipes and wait for children
    close all pipe file descriptors in parent
    */
    for(int i = 0; i < numStages - 1; i++){
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    //wait for the last process
    int status;
    waitpid(pids[numStages-1], &status, 0);
    
    //wait for all other children to avoid zombie processes
    for(int i = 0; i < numStages; i++){
        if(i != numStages-1){                       //don't wait twice for last process
            waitpid(pids[i], &status, 0);
        }
    }
}
