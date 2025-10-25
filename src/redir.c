#include "redir.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/*helper function for file redirection that redirects file to standard streams
This function opens a file and redirects it to stdin, stdout, or stderr
Returns 0 on success, -1 on failure
*/
int setup_redirection(const char *filename, int flags, int target_fd) {
    int fd = open(filename, flags, 0644);
    if(fd < 0){
        //use the target stream, not flags (O_RDONLY is 0 on POSIX)
        if(target_fd == STDIN_FILENO){
            //assignment requires this exact message on stdout
            printf("File not found.\n");
        }else{
            //keep perror for output/error file issues
            perror("bad file");
        }
        return -1;
    }

    if(dup2(fd, target_fd) < 0){
        perror("dup2 failed");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}
