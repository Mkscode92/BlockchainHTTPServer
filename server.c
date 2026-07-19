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
    int new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);

    const char *message =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 15\r\n"
        "\r\n"
        "Hello, Testing\n";
    send(new_fd, message, strlen(message), 0);

    while(1){ 
        // Some kind of request must be received here 
        
        printf("Waiting for request ... \n");
        sleep(1);
    };
}



