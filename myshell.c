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

//forward function declarations to tell compiler about functions defined later
int parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile, int isPipeline);
int validate_pipeline(char *cmd);
void execute_command(char *args[], char *inputFile, char *outputFile, char *errorFile);
void execute_pipeline(char *cmd);
static int setup_redirection(const char *filename, int flags, int target_fd);

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

/*utility function to check if a string is empty after removing leading/trailing whitespace
This function determines if a string contains only whitespace characters
*/
static int is_empty_after_trim(char *str){
    str = skip_whitespace(str);             //skip leading whitespace
    return *str == '\0';                    //return true if we reached end of string (empty)
}

/*pipeline validation function to check if pipeline syntax is correct
This function validates that pipes are used correctly and there are no empty commands. Returns 0 for valid pipeline, -1 for invalid pipeline with error message printed
*/
int validate_pipeline(char *cmd){
    //skip leading whitespace to find actual start of command
    char *p = skip_whitespace(cmd);
    
    //check for leading pipe (command starts with |)
    if(*p == '|'){
        printf("Command missing after pipe.\n");
        return -1;
    }
    
    //check for trailing pipe
    int len = strlen(p);
    //work backwards from end, skipping trailing whitespace
    while(len > 0 && (p[len-1] == ' ' || p[len-1] == '\t' || p[len-1] == '\n')){
        len--;
    }
    //if last non-whitespace character is |, it's invalid
    if(len > 0 && p[len-1] == '|'){
        printf("Command missing after pipe.\n");
        return -1;
    }
    
    //check for empty commands between pipes
    char *saveptr;
    char *token = strtok_r(p, "|", &saveptr);               //split by pipe character
    while(token != NULL){
        //check if this token (command between pipes) is empty after trimming whitespace
        if(is_empty_after_trim(token)){
            printf("Empty command between pipes.\n");
            return -1;
        }
        token = strtok_r(NULL, "|", &saveptr);                  //get next token
    }
    
    return 0;
}

//structure definition for pipeline stages
//each stage in a pipeline is a separate command with its own arguments and redirections
typedef struct {
    char *args[MAX_ARGS];
    char *inputFile;
    char *outputFile;
    char *errorFile;
} Stage;

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
            connect input from previous stage
            */
            if(i > 0 && stages[i].inputFile == NULL){
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            if(i < numStages - 1 && stages[i].outputFile == NULL){
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            //close all pipe file descriptors in child
            for(int j = 0; j < numStages - 1; j++){
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            //handle explicit file redirections (override pipe connections)
            if(stages[i].inputFile && setup_redirection(stages[i].inputFile, O_RDONLY, STDIN_FILENO) < 0){
                exit(EXIT_FAILURE);
            }
            if(stages[i].outputFile && setup_redirection(stages[i].outputFile, O_WRONLY|O_CREAT|O_TRUNC, STDOUT_FILENO) < 0){
                exit(EXIT_FAILURE);
            }
            if(stages[i].errorFile && setup_redirection(stages[i].errorFile, O_WRONLY|O_CREAT|O_TRUNC, STDERR_FILENO) < 0){
                exit(EXIT_FAILURE);
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

/*command parsing function that extracts command arguments and redirection information
tokenizes the input command and identifies redirection symbols
returns 0 on success, -1 on error (with args[0] set to NULL)
*/
int parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile, int isPipeline){
    int i = 0;
    *inputFile = *outputFile = *errorFile = NULL;
    
    char *token = strtok(cmd, " \t\n");
    while (token != NULL) {
        //handle redirection symbols
        if (strcmp(token, "<") == 0) {
            char *filename = strtok(NULL, " \t\n");
            if (filename == NULL) {
                printf("Input file not specified.\n");
                args[0] = NULL;
                return -1;
            }
            *inputFile = filename;
            token = strtok(NULL, " \t\n");
            continue;
        } else if (strcmp(token, ">") == 0) {
            char *filename = strtok(NULL, " \t\n");
            if (filename == NULL) {
                //no filename provided after > symbol, different error message for pipelines vs single commands
                if (isPipeline) {
                    printf("Output file not specified after redirection.\n");
                } else {
                    printf("Output file not specified.\n");
                }
                args[0] = NULL;
                return -1;
            }
            *outputFile = filename;
            token = strtok(NULL, " \t\n");
            continue;
        } 
        //handle error redirection symbol (2>)
        else if (strcmp(token, "2>") == 0) {
            char *filename = strtok(NULL, " \t\n");
            if (filename == NULL) {
                printf("Error output file not specified.\n");
                args[0] = NULL;
                return -1;
            }
            *errorFile = filename;
            token = strtok(NULL, " \t\n");
            continue;
        }
        
        //regular argument (not a redirection symbol)
        //check if we have room for more arguments
        if (i >= MAX_ARGS - 1) {
            printf("Too many arguments.\n");
            args[0] = NULL;
            return -1;
        }
        //add this token to the arguments array
        args[i++] = token;
        token = strtok(NULL, " \t\n");
    }
    
    //null-terminate the arguments array (required by execvp)
    args[i] = NULL;
    
    //check if we have at least one argument (command name)
    if (i == 0) {
        args[0] = NULL;
        return -1;
    }
    return 0;
}

/*helper function for file redirection that redirects file to standard streams
This function opens a file and redirects it to stdin, stdout, or stderr
Returns 0 on success, -1 on failure
*/
static int setup_redirection(const char *filename, int flags, int target_fd) {
    //open the file with specified flags and permissions
    //flags: O_RDONLY for input, O_WRONLY|O_CREAT|O_TRUNC for output
    //0644: file permissions (read/write for owner, read for group/others)
    int fd = open(filename, flags, 0644);
    
    if (fd < 0) {
        if (flags & O_RDONLY) {
            printf("File not found.\n");
        } else {
            perror("bad file");
        }
        return -1;
    }
    
    //redirect the file descriptor to the target stream (stdin/stdout/stderr)
    //dup2 makes target_fd point to the same file as fd
    if (dup2(fd, target_fd) < 0) {
        perror("dup2 failed");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
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
    
    if (pid == 0) {
        /*child process : execute the command with redirections
        set up input redirection if specified
        redirect stdin to read from inputFile
        */
        if (inputFile && setup_redirection(inputFile, O_RDONLY, STDIN_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        
        //set up output redirection if specified
        //redirect stdout to write to outputFile
        if (outputFile && setup_redirection(outputFile, O_WRONLY|O_CREAT|O_TRUNC, STDOUT_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        
        //set up error redirection if specified
        //redirect stderr to write to errorFile
        if (errorFile && setup_redirection(errorFile, O_WRONLY|O_CREAT|O_TRUNC, STDERR_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        
        /*execute the command using execvp
        execvp replaces the current process with the new command
        it searches PATH environment variable for the executable
        */
        if (execvp(args[0], args) < 0) {
            printf("Command not found.\n");
            exit(EXIT_FAILURE);
        }
    } else {
        /*parent process : wait for child to complete
        wait() blocks until child process terminates
        this ensures the shell waits for command completion before showing next prompt
        */
        wait(NULL);
    }
}

/*Main function
This function implements the main shell loop that reads commands and executes them
It handles both single commands and pipelines, with proper error handling
*/
int main() {
    //buffer to store user input command
    char cmd[MAX_CMD_LENGTH];
    //array to store parsed command arguments
    char *args[MAX_ARGS];
    //point er to store redirection filenames
    char *inputFile, *outputFile, *errorFile;
    
    while (1) {
        //display shell prompt
        printf("$ ");
        
        //read command from user input
        if (fgets(cmd, MAX_CMD_LENGTH, stdin) == NULL) {
            break;
        }
        
        //remove newline and skip empty commands
        cmd[strcspn(cmd, "\n")] = '\0';
        
        //skip empty commands
        if(strlen(cmd) == 0){
            continue;
        }
        
        //handle exit command
        if(strcmp(cmd, "exit") == 0){
            break;
        }
        
        //execute command (pipeline or single)
        if(strchr(cmd, '|') != NULL){
            //command contains pipe symbol - execute as pipeline
            execute_pipeline(cmd);
        }else if(parse_command(cmd, args, &inputFile, &outputFile, &errorFile, 0) == 0){
            //single command - parse and execute if parsing succeeded
            execute_command(args, inputFile, outputFile, errorFile);
        }
    }
    
    return 0;
}
