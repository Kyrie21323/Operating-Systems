#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

//maximum length for command input buffer
#define MAX_CMD_LENGTH 1024 

//global variable for signal handling
static int client_fd = -1;

//signal handler for graceful shutdown, closes socket and exits cleanly
void signal_handler(int sig){
    printf("\n[INFO] Shutting down client...\n");
    if(client_fd >= 0){
        close_socket(client_fd);
    }
    exit(0);
}

//client main function, connects to server, displays prompt, reads commands, and sends them
int main(int argc, char *argv[]){
    char *server_ip;
    int port;
    char cmd_buffer[MAX_CMD_LENGTH];

    //check command line arguments
    if(argc != 3){
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(1);
    }

    server_ip = argv[1];
    port = atoi(argv[2]);
    
    if(port <= 0 || port > 65535){
        fprintf(stderr, "Error: Invalid port number\n");
        exit(1);
    }

    //set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    //connect to server
    client_fd = create_client_socket(server_ip, port);
    if(client_fd < 0){
        fprintf(stderr, "Error: Failed to connect to server\n");
        exit(1);
    }

    printf("[INFO] Connected to server successfully\n");

    //main client loop
    while(1){
        //display prompt
        printf("$ ");
        fflush(stdout);

        //read command from user input
        if(fgets(cmd_buffer, sizeof(cmd_buffer), stdin) == NULL){
            //handle Ctrl+D (EOF)
            printf("\n[INFO] End of input, exiting...\n");
            break;
        }

        //remove newline character
        cmd_buffer[strcspn(cmd_buffer, "\n")] = '\0';

        //skip empty commands
        if(strlen(cmd_buffer) == 0){
            continue;
        }

        //send command to server
        if(send_line(client_fd, cmd_buffer) < 0){
            perror("Error sending command");
            break;
        }

        printf("[INFO] Command sent to server: \"%s\"\n", cmd_buffer);

        //handle exit command
        if(strcmp(cmd_buffer, "exit") == 0){
            printf("[INFO] Exiting client...\n");
            break;
        }
    }

    //clean up
    close_socket(client_fd);
    return 0;
}
