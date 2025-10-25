#include "tokenize.h"
#include "util.h"
#include <glob.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

//maximum length for command input buffer
#define MAX_CMD_LENGTH 1024 
//maximum number of arguments a command can have
#define MAX_ARGS 64         

/* Returns 0 on success, -1 on unclosed quote or OOM.
   On success, *out = heap array of QToks (count elements). Caller frees.
*/
int qtokenize(const char *line, QTok **out, int *count){
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

void free_qtokens(QTok *arr, int n){ for(int i=0;i<n;i++) free(arr[i].val); free(arr); }

/* Expand * ? [ ] on unquoted argv words using glob(3).
   Keeps redirection filenames unexpanded.
*/
void apply_globbing(char **argv, bool *was_quoted, int *argc){
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
