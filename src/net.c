#include "net.h"

//creates and binds a server socket to the specified port, returns socket file descriptor on success, -1 on failure
int create_server_socket(int port){
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    //create socket file descriptor
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0){
        perror("socket failed");
        return -1;
    }

    //set socket options to allow address reuse
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))){
        perror("setsockopt failed");
        close(server_fd);
        return -1;
    }

    //configure address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    //bind socket to address
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0){
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    //start listening for connections
    if(listen(server_fd, 3) < 0){
        perror("listen failed");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

//accepts a client connection on the server socket, blocks until a client connects, returns client socket file descriptor on success, -1 on failure
int accept_client_connection(int server_fd){
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int client_fd;

    printf("[INFO] Waiting for client connection...\n");
    
    if((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0){
        perror("accept failed");
        return -1;
    }

    printf("[INFO] Client connected from %s:%d\n", 
           inet_ntoa(address.sin_addr), ntohs(address.sin_port));
    
    return client_fd;
}

//creates and connects a client socket to the specified server, establishes connection to the server, returns socket file descriptor on success, -1 on failure
int create_client_socket(const char *server_ip, int port){
    int client_fd;
    struct sockaddr_in serv_addr;

    //create socket file descriptor
    if((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket creation failed");
        return -1;
    }

    //configure server address structure
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    //convert IP address from string to binary format
    if(inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0){
        perror("invalid address/address not supported");
        close(client_fd);
        return -1;
    }

    //connect to server
    if(connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        perror("connection failed");
        close(client_fd);
        return -1;
    }

    printf("[INFO] Connected to server %s:%d\n", server_ip, port);
    return client_fd;
}

//sends a line of text over the socket, sends the entire line including null terminator, returns number of bytes sent on success, -1 on failure
int send_line(int socket_fd, const char *line){
    int len = strlen(line);
    int total_sent = 0;
    int bytes_sent;

    //send the line length first (as 4-byte integer)
    int line_len = htonl(len);
    if(send(socket_fd, &line_len, sizeof(line_len), 0) < 0){
        perror("send length failed");
        return -1;
    }

    //send the actual line data
    while(total_sent < len){
        bytes_sent = send(socket_fd, line + total_sent, len - total_sent, 0);
        if(bytes_sent < 0){
            perror("send data failed");
            return -1;
        }
        total_sent += bytes_sent;
    }

    return total_sent;
}

//receives a line of text from the socket, reads the line length first, then the actual data, returns number of bytes received on success, -1 on failure, 0 on connection closed
int receive_line(int socket_fd, char *buffer, int buffer_size){
    int line_len;
    int total_received = 0;
    int bytes_received;

    //receive the line length first
    if(recv(socket_fd, &line_len, sizeof(line_len), MSG_WAITALL) < 0){
        perror("receive length failed");
        return -1;
    }

    line_len = ntohl(line_len);
    
    //check if the line is too long for our buffer
    if(line_len >= buffer_size){
        fprintf(stderr, "Received line too long (%d bytes)\n", line_len);
        return -1;
    }

    //receive the actual line data
    while(total_received < line_len){
        bytes_received = recv(socket_fd, buffer + total_received, line_len - total_received, MSG_WAITALL);
        if(bytes_received <= 0){
            if(bytes_received == 0){
                printf("[INFO] Client disconnected\n");
                return 0;                                   //connection closed
            }
            perror("receive data failed");
            return -1;
        }
        total_received += bytes_received;
    }

    buffer[line_len] = '\0';                                //null terminate the received string
    return total_received;
}

//closes a socket connection, properly closes the socket file descriptor
void close_socket(int socket_fd){
    if(socket_fd >= 0){
        close(socket_fd);
    }
}
