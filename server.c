#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>

// http is the protocol that serves the internet --> allows you to transfer files over internet
// url -> IP address (target machine, holds files we want to access) -> send request via HTTP over 
// TCP/IP stack (routing) telling it to retrive the contents of the website 
// gcc server.c -o server.exe && ./server.exe
/*
gcc -o server server.c 
./server 
*/

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd  <  0){ 
        perror("Socket error.");
        exit(1);
    }
    // bind(); // ties socket to an address / port 
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;  // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

    while(1){ 
        // Some kind of request must be received here 
        printf("Waiting for request ... \n");
        sleep(1);
    };
}



