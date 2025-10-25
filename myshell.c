#include <stdio.h>    
#include <stdlib.h>   
#include <string.h>   
#include <unistd.h>   
#include <sys/wait.h> 
#include <fcntl.h>
#include <glob.h>
#include <stdbool.h>
#include <errno.h>    

/* Portable strdup to avoid feature-macro surprises */
static char *xstrdup(const char *s){
    if(!s){
        return NULL;
    }
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if(!p){
        perror("malloc"); _exit(127);
    }
    memcpy(p, s, n);
    return p;
}

//token record for quote-aware parsing
typedef struct {
    char *val;
    bool was_quoted;   //true if produced by quotes ('' or "")
} QTok;

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


/*pipeline validation function to check if pipeline syntax is correct
This function validates that pipes are used correctly and there are no empty commands. Returns 0 for valid pipeline, -1 for invalid pipeline with error message printed
*/
int validate_pipeline(char *cmd){
    //work on a copy so we don't modify the original
    size_t n = strlen(cmd) + 1;
    char buf[n];
    memcpy(buf, cmd, n);

    //trim leading whitespace
    char *p = skip_whitespace(buf);

    //leading pipe check
    if (*p == '|') {
        printf("Command missing after pipe.\n");
        return -1;
    }

    //scan to detect empties between pipes and trailing pipe
    int saw_non_ws_since_pipe = 0;
    for (char *q = p; *q; ++q) {
        if (*q == '|') {
            //if we hit a pipe and haven't seen a non-ws char since the last pipe, it's empty
            if (!saw_non_ws_since_pipe) {
                printf("Empty command between pipes.\n");
                return -1;
            }
            saw_non_ws_since_pipe = 0; //reset for the next segment
        } else if (*q != ' ' && *q != '\t' && *q != '\n') {
            saw_non_ws_since_pipe = 1;
        }
    }

    //if the last segment had no non-ws (i.e., ended with '|' or only spaces after it)
    if (!saw_non_ws_since_pipe) {
        printf("Command missing after pipe.\n");
        return -1;
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

/* Returns 0 on success, -1 on unclosed quote or OOM.
   On success, *out = heap array of QToks (count elements). Caller frees.
*/
static int qtokenize(const char *line, QTok **out, int *count){
    *out=NULL; *count=0;
    const char *p=line;
    bool in_s=false, in_d=false;
    char buf[MAX_CMD_LENGTH];
    int bl=0;

    int cap=16, n=0;
    QTok *arr = (QTok*)malloc(cap*sizeof(QTok));
    if(!arr){ perror("malloc"); return -1; }

    while(*p){
        //skip ws when not in quotes and not in a token
        while(!in_s && !in_d && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) p++;
        if(!*p) break;

        bool was_quoted=false;
        bl=0;

        while(*p){
            if(in_s){
                if(*p=='\''){ in_s=false; was_quoted=true; p++; continue; }
                if(bl>=MAX_CMD_LENGTH-1){ free(arr); return -1; }
                buf[bl++]=*p++;
            } else if(in_d){
                if(*p=='"'){ in_d=false; was_quoted=true; p++; continue; }
                if(*p=='\\' && (p[1]=='"'||p[1]=='\\')){ p++; if(bl>=MAX_CMD_LENGTH-1){ free(arr); return -1; } buf[bl++]=*p++; }
                else { if(bl>=MAX_CMD_LENGTH-1){ free(arr); return -1; } buf[bl++]=*p++; }
            } else {
                if(*p=='\''){ in_s=true; p++; continue; }
                if(*p=='"'){ in_d=true; p++; continue; }
                if(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') break; /* token end */
                if(*p=='|'||*p=='<'||*p=='>'){
                    //operator ends token if we started; otherwise emit operator as its own token outside this loop
                    if(bl==0) break;
                    else break;
                }
                if(bl>=MAX_CMD_LENGTH-1){ free(arr); return -1; }
                buf[bl++]=*p++;
            }
        }

        //emit token if we captured any or if it was empty-quoted
        if(bl>0 || was_quoted){
            if(n==cap){ cap*=2; QTok *tmp=realloc(arr, cap*sizeof(QTok)); if(!tmp){ perror("realloc"); free(arr); return -1;} arr=tmp; }
            buf[bl]='\0';
            arr[n].val = xstrdup(buf);
            arr[n].was_quoted = was_quoted;
            n++;
        }

        //outside quotes: treat single-char | < > and two-char 2> as separate tokens
        if(!in_s && !in_d){
            while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
            if(*p=='2' && p[1]=='>'){
                if(n==cap){ cap*=2; QTok *tmp=realloc(arr, cap*sizeof(QTok)); if(!tmp){ perror("realloc"); free(arr); return -1;} arr=tmp; }
                arr[n].val = xstrdup("2>");
                arr[n].was_quoted=false; n++; p+=2;
            } else if(*p=='|'||*p=='<'||*p=='>'){
                char op[2]={*p,0};
                if(n==cap){ cap*=2; QTok *tmp=realloc(arr, cap*sizeof(QTok)); if(!tmp){ perror("realloc"); free(arr); return -1;} arr=tmp; }
                arr[n].val = xstrdup(op);
                arr[n].was_quoted=false; n++; p++;
            }
        }
    }

    if(in_s||in_d){             //unclosed quote
        for(int i=0;i<n;i++) free(arr[i].val);
        free(arr);
        return -1;
    }

    *out=arr; *count=n; return 0;
}

static void free_qtokens(QTok *arr, int n){ for(int i=0;i<n;i++) free(arr[i].val); free(arr); }

//strip one pair of outer quotes from a string if present
static char *strip_outer_quotes(const char *str) {
    if(!str){
        return NULL;
    }
    size_t len = strlen(str);
    if(len < 2){
        return xstrdup(str);
    }
    
    //check for single quotes
    if(str[0] == '\'' && str[len-1] == '\''){
        char *result = malloc(len - 1);
        if(!result){
            perror("malloc");
            _exit(127);
        }
        memcpy(result, str + 1, len - 2);
        result[len - 2] = '\0';
        return result;
    }
    
    //check for double quotes
    if(str[0] == '"' && str[len-1] == '"'){
        char *result = malloc(len - 1);
        if(!result){
            perror("malloc"); _exit(127);
        }
        memcpy(result, str + 1, len - 2);
        result[len - 2] = '\0';
        return result;
    }
    
    //no outer quotes, return copy
    return xstrdup(str);
}

/* Expand * ? [ ] on unquoted argv words using glob(3).
   Keeps redirection filenames unexpanded.
*/
static void apply_globbing(char **argv, bool *was_quoted, int *argc){
    char *outv[MAX_ARGS]; bool outq[MAX_ARGS];
    int m=0;

    for(int i=0;i<*argc;i++){
        char *w = argv[i];
        bool q = was_quoted[i];

        //detect redirection markers and skip the following filename
        if((strcmp(w,"<")==0)||(strcmp(w,">")==0)||(strcmp(w,"2>")==0)){
            if(m<MAX_ARGS-1){ outv[m]=w; outq[m]=false; m++; }
            if(i+1<*argc){ outv[m]=argv[i+1]; outq[m]=true; m++; i++; }          //filename as-is
            continue;
        }

        if(q){
            //keep quoted tokens as-is
            if(m<MAX_ARGS-1){ outv[m]=w; outq[m]=true; m++; }
            continue;
        }

        //check for glob chars
        bool hasg=false;
        for(char *p=w; *p; ++p){ if(*p=='*'||*p=='?'||*p=='['||*p==']'){ hasg=true; break; } }

        if(!hasg){
            if(m<MAX_ARGS-1){ outv[m]=w; outq[m]=false; m++; }
            continue;
        }

        glob_t gr; memset(&gr,0,sizeof(gr));
        int rc = glob(w, GLOB_NOCHECK, NULL, &gr);
        if(rc==0){
            for(size_t j=0;j<gr.gl_pathc && m<MAX_ARGS-1;j++){
                outv[m]=xstrdup(gr.gl_pathv[j]); outq[m]=false; m++;
            }
            free(w);             //was original token; replaced by duplicates
        }else{
            //fallback: keep as-is
            if(m<MAX_ARGS-1){ outv[m]=w; outq[m]=false; m++; }
        }
        globfree(&gr);
    }

    //write back
    for(int i=0;i<m;i++){ argv[i]=outv[i]; was_quoted[i]=outq[i]; }
    *argc=m; argv[m]=NULL;
}

/*command parsing function that extracts command arguments and redirection information
tokenizes the input command and identifies redirection symbols
returns 0 on success, -1 on error (with args[0] set to NULL)
*/
int parse_command(char *cmd, char *args[], char **inputFile, char **outputFile, char **errorFile, int isPipeline){
    *inputFile = *outputFile = *errorFile = NULL;

    QTok *toks=NULL; int nt=0;
    if(qtokenize(cmd, &toks, &nt)!=0){
        printf("Unclosed quotes.\n");
        args[0]=NULL; return -1;
    }
    if(nt==0){ args[0]=NULL; free_qtokens(toks,nt); return -1; }

    //first pass: copy into args[] + parallel quoted flags
    bool quoted[MAX_ARGS]; int ac=0;
    for(int i=0;i<nt;i++){
        if(ac>=MAX_ARGS-1){ printf("Too many arguments.\n"); free_qtokens(toks,nt); args[0]=NULL; return -1; }
        args[ac] = toks[i].val;             //take ownership of the string
        quoted[ac] = toks[i].was_quoted;
        ac++;
    }
    //toks array structure is no longer needed, but strings are now in args[], so free only the array
    free(toks);

    //validate redirections have filenames (only for unquoted operators)
    for(int i=0;i<ac;i++){
        if(!quoted[i] && (strcmp(args[i],"<")==0 || strcmp(args[i],">")==0 || strcmp(args[i],"2>")==0)){
            if(i+1>=ac){ 
                if(strcmp(args[i],"<")==0) printf("Input file not specified.\n");
                else if(strcmp(args[i],">")==0) printf(isPipeline ? "Output file not specified after redirection.\n" : "Output file not specified.\n");
                else printf("Error output file not specified.\n");
                //free any heap strings in args[]
                for(int k=0;k<ac;k++) free(args[k]);
                args[0]=NULL; return -1;
            }
        }
    }

    //extract redirection filenames & remove the operator/filename pairs from argv (only for unquoted operators)
    char *argv2[MAX_ARGS]; bool quoted2[MAX_ARGS]; int m=0;
    for(int i=0;i<ac;i++){
        if(!quoted[i] && strcmp(args[i],"<")==0){
            *inputFile = strip_outer_quotes(args[i+1]); i++; continue;
        }else if(!quoted[i] && strcmp(args[i],">")==0){
            *outputFile = strip_outer_quotes(args[i+1]); i++; continue;
        }else if(!quoted[i] && strcmp(args[i],"2>")==0){
            *errorFile = strip_outer_quotes(args[i+1]); i++; continue;
        }else{
            argv2[m]=args[i]; quoted2[m]=quoted[i]; m++;
        }
    }
    argv2[m]=NULL;

    if(m==0){             //no command
        if(*inputFile){ free(*inputFile); *inputFile=NULL; }
        if(*outputFile){ free(*outputFile); *outputFile=NULL; }
        if(*errorFile){ free(*errorFile); *errorFile=NULL; }
        args[0]=NULL; return -1;
    }

    //apply globbing on unquoted argv words (NOT on redirection filenames)
    apply_globbing(argv2, quoted2, &m);

    //Copy back into args[]
    for(int i=0;i<m;i++) args[i]=argv2[i];
    args[m]=NULL;

    return 0;
}

/*helper function for file redirection that redirects file to standard streams
This function opens a file and redirects it to stdin, stdout, or stderr
Returns 0 on success, -1 on failure
*/
static int setup_redirection(const char *filename, int flags, int target_fd) {
    int fd = open(filename, flags, 0644);
    if (fd < 0) {
        //use the target stream, not flags (O_RDONLY is 0 on POSIX)
        if (target_fd == STDIN_FILENO) {
            //assignment requires this exact message on stdout
            printf("File not found.\n");
        } else {
            //keep perror for output/error file issues
            perror("bad file");
        }
        return -1;
    }

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
            //free argv strings created by qtokenize/globbing
            for(int i=0; args[i]!=NULL; i++){
                free(args[i]);
            }
            if(inputFile){
                free(inputFile);
            }
            if(outputFile){
                free(outputFile);
            }
            if(errorFile){
                free(errorFile);
            }
        }
    }
    
    return 0;
}
