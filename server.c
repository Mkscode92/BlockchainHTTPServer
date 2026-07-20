#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>

// http is the protocol that serves the internet --> allows you to transfer files over internet
// url -> IP address (target machine, holds files we want to access) -> send request via HTTP over 
// TCP/IP stack (routing) telling it to retrive the contents of the website 
// gcc server.c -o server.exe && ./server.exe
/*
gcc -o server server.c 
./server 
*/

#define BACKLOG 20
#define PORT "8080"

int main() {
    struct addrinfo hints, *res; 
    int sockfd; 

    //load up addresss structs with getaddrinfo 
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;  // IPV4, use AF_UNSPEC for either
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

    getaddrinfo(NULL, PORT, &hints, &res); //8080 doesnt need admin 
    //NULL = you want your own machine's address instead, not remote host

    // make a socket 
    // sockfd = socket(AF_INET, SOCK_STREAM, 0);
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd  <  0){ 
        perror("Socket error.");
        exit(1);
    }

    int bind_result = bind(sockfd, res->ai_addr, res->ai_addrlen); 
    // ties socket to an address / port 
    printf("Bind result is: %d\n", bind_result);

    //Listen for incoming connections 
    int listen_res = listen(sockfd, BACKLOG); 
    if (listen_res < 0){ 
        perror("Listen error oh boy no bueno"); 
        exit(1); 
    }

    //accept a pending connection from a client 
    struct sockaddr_storage client_addr; 
    socklen_t addr_size; 
    addr_size = sizeof client_addr; 
    while (1){ 
        int new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);

        int max_len = 1000; 
        char received_request[max_len]; 
        memset(received_request, 0, max_len);
        int num_bytes = recv(new_fd, received_request, max_len, 0);
        // printf("Received Request:\n%s", received_request);
        
        //GET request, send a response 
        if (strncmp(received_request, "GET", 3) == 0){
            printf("Received HTTP GET request!\n");
            //respond to the HTTP GET request by serving index.html

            FILE *file = fopen("index.html", "rb");
            if (file == NULL) {
                const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                send(new_fd, not_found, strlen(not_found), 0);
            } else {
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                rewind(file);

                char *file_contents = malloc(file_size);
                fread(file_contents, 1, file_size, file);
                fclose(file);

                char headers[256];
                int headers_len = snprintf(headers, sizeof headers,
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Content-Length: %ld\r\n"
                    "\r\n",
                    file_size);

                send(new_fd, headers, headers_len, 0);
                send(new_fd, file_contents, file_size, 0);

                free(file_contents);
            }
        }
        else { 
            printf("Received Non-GET request. Ignoring ...."); 
            close(new_fd);
            exit(-1); 
        }
        close(new_fd); 
    }
    close(sockfd); 
}