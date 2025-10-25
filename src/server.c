#include "net.h"
#include "parse.h"
#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

//maximum length for command input buffer
#define MAX_CMD_LENGTH 1024 
//maximum number of arguments a command can have
#define MAX_ARGS 64         

//global variables for signal handling
static int server_fd = -1;
static int client_fd = -1;

//signal handler for graceful shutdown, closes sockets and exits cleanly
void signal_handler(int sig){
    printf("\n[INFO] Shutting down server...\n");
    if(client_fd >= 0){
        close_socket(client_fd);
    }
    if(server_fd >= 0){
        close_socket(server_fd);
    }
    exit(0);
}

//server main function, sets up socket, accepts client connections, and processes commands
int main(int argc, char *argv[]) {
    int port;
    char cmd_buffer[MAX_CMD_LENGTH];
    char *args[MAX_ARGS];
    char *inputFile, *outputFile, *errorFile;

    //check command line arguments
    if(argc != 2){
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    port = atoi(argv[1]);
    if(port <= 0 || port > 65535){
        fprintf(stderr, "Error: Invalid port number\n");
        exit(1);
    }

    //set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    //create server socket
    server_fd = create_server_socket(port);
    if(server_fd < 0){
        fprintf(stderr, "Error: Failed to create server socket\n");
        exit(1);
    }

    printf("[INFO] Server started on port %d\n", port);

    //main server loop
    while (1){
        //accept client connection
        client_fd = accept_client_connection(server_fd);
        if(client_fd < 0){
            fprintf(stderr, "Error: Failed to accept client connection\n");
            continue;
        }

        printf("[INFO] Client session started\n");

        //process commands from client
        while(1){
            //receive command from client
            int bytes_received = receive_line(client_fd, cmd_buffer, sizeof(cmd_buffer));
            
            if(bytes_received <= 0){
                if(bytes_received == 0){
                    printf("[INFO] Client disconnected\n");
                }else{
                    perror("Error receiving command");
                }
                break;
            }

            //log the received command
            printf("[RECEIVED] Received command: \"%s\" from client.\n", cmd_buffer);

            //handle exit command
            if(strcmp(cmd_buffer, "exit") == 0){
                printf("[INFO] Client requested exit\n");
                break;
            }

            //skip empty commands
            if(strlen(cmd_buffer) == 0){
                continue;
            }

            //execute command using existing shell functions
            if(strchr(cmd_buffer, '|') != NULL){
                //pipeline command
                printf("[INFO] Executing pipeline command\n");
                execute_pipeline(cmd_buffer);
            }else if(parse_command(cmd_buffer, args, &inputFile, &outputFile, &errorFile, 0) == 0){
                //single command
                printf("[INFO] Executing single command\n");
                execute_command(args, inputFile, outputFile, errorFile);
                
                //free memory allocated by parse_command
                for(int i = 0; args[i] != NULL; i++){
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
            }else{
                printf("[INFO] Command parsing failed\n");
            }
        }

        //close client connection
        close_socket(client_fd);
        client_fd = -1;
        printf("[INFO] Client session ended\n");
    }

    //clean up
    close_socket(server_fd);
    return 0;
}
