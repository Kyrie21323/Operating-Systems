#ifndef NET_H
#define NET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

//maximum buffer size for network communication
#define MAX_BUFFER_SIZE 1024

//socket helper functions for client-server communication
//creates and binds a server socket to the specified port, returns socket file descriptor on success, -1 on failure
int create_server_socket(int port);

//accepts a client connection on the server socket, returns client socket file descriptor on success, -1 on failure
int accept_client_connection(int server_fd);

//creates and connects a client socket to the specified server, returns socket file descriptor on success, -1 on failure
int create_client_socket(const char *server_ip, int port);

//sends a line of text over the socket, returns number of bytes sent on success, -1 on failure
int send_line(int socket_fd, const char *line);

//receives a line of text from the socket, returns number of bytes received on success, -1 on failure, 0 on connection closed
int receive_line(int socket_fd, char *buffer, int buffer_size);

//closes a socket connection
void close_socket(int socket_fd);

#endif
