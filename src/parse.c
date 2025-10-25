#include "parse.h"
#include "tokenize.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//maximum length for command input buffer
#define MAX_CMD_LENGTH 1024 
//maximum number of arguments a command can have
#define MAX_ARGS 64         

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
