#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <pthread.h>
#include "blockchain.h"

// http is the protocol that serves the internet --> allows you to transfer files over internet
// url -> IP address (target machine, holds files we want to access) -> send request via HTTP over 
// TCP/IP stack (routing) telling it to retrive the contents of the website 
// gcc server.c -o server.exe && ./server.exe
/*
gcc -Wall -pthread -o server server.c blockchain.c -lm
./server
*/

#define BACKLOG 20
#define PORT "8080"

static void send_response(int fd, const char *status, const char *content_type, const char *body) {
    size_t body_len = strlen(body);
    char headers[256];
    int headers_len = snprintf(headers, sizeof headers,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, content_type, body_len);
    send(fd, headers, headers_len, 0);
    send(fd, body, body_len, 0);
}

// sends a malloc'd JSON string and frees it
static void send_json(int fd, char *json) {
    send_response(fd, "200 OK", "application/json", json);
    free(json);
}

// pulls key out of a "a=1&b=2" string (query string or form body). returns 1 if found
static int get_param(const char *src, const char *key, char *out, size_t out_len) {
    if (src == NULL) return 0;
    size_t key_len = strlen(key);
    const char *p = src;
    while (*p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            p += key_len + 1;
            size_t i = 0;
            while (*p && *p != '&' && *p != '\r' && *p != '\n' && i < out_len - 1)
                out[i++] = *p++;
            out[i] = '\0';
            return 1;
        }
        p = strchr(p, '&');
        if (p == NULL) break;
        p++;
    }
    return 0;
}

// blockchain API routes. returns 1 if the path was handled, 0 to fall through to the file server
static int handle_chain_route(int fd, const char *method, const char *path, const char *query, const char *body) {
    int is_get = strcmp(method, "GET") == 0;
    int is_post = strcmp(method, "POST") == 0;

    if (is_get && strcmp(path, "/chain") == 0)    { send_json(fd, chain_to_json()); return 1; }
    if (is_get && strcmp(path, "/pending") == 0)  { send_json(fd, chain_pending_json()); return 1; }
    if (is_get && strcmp(path, "/validate") == 0) { send_json(fd, chain_validate_json()); return 1; }
    if (is_get && strcmp(path, "/risk") == 0)     { send_json(fd, chain_risk_json()); return 1; }

    if (is_post && strcmp(path, "/transactions") == 0) {
        // accept params from the form body (curl -d) or the query string
        const char *src = (body && *body) ? body : query;
        char sender[ADDR_LEN], recipient[ADDR_LEN], amount[32];
        if (!get_param(src, "sender", sender, sizeof sender) ||
            !get_param(src, "recipient", recipient, sizeof recipient) ||
            !get_param(src, "amount", amount, sizeof amount) ||
            chain_add_transaction(sender, recipient, atof(amount)) != 0) {
            send_response(fd, "400 Bad Request", "application/json",
                "{\"error\":\"need sender, recipient (letters, digits, _-.) and a positive amount; pool may be full\"}");
        } else {
            send_response(fd, "201 Created", "application/json", "{\"status\":\"queued\"}");
        }
        return 1;
    }

    if (is_post && strcmp(path, "/mine") == 0) {
        int idx = chain_mine();
        if (idx < 0) {
            send_response(fd, "400 Bad Request", "application/json", "{\"error\":\"no pending transactions\"}");
        } else {
            char msg[64];
            snprintf(msg, sizeof msg, "{\"status\":\"mined\",\"block\":%d}", idx);
            send_response(fd, "201 Created", "application/json", msg);
        }
        return 1;
    }

    // demo only: rewrites history so /validate and /risk can show the tampering being caught
    if (is_post && strcmp(path, "/tamper") == 0) {
        const char *src = (body && *body) ? body : query;
        char block[16], amount[32];
        if (!get_param(src, "block", block, sizeof block) ||
            !get_param(src, "amount", amount, sizeof amount) ||
            chain_tamper(atoi(block), atof(amount)) != 0) {
            send_response(fd, "400 Bad Request", "application/json",
                "{\"error\":\"need block (>=1, with transactions) and amount\"}");
        } else {
            send_response(fd, "200 OK", "application/json", "{\"status\":\"tampered\"}");
        }
        return 1;
    }

    return 0;
}

void *handle_client(void *arg) {
    int new_fd = *(int *)arg;
    free(arg);

    int max_len = 8192;
    char received_request[max_len];
    memset(received_request, 0, max_len);
    int num_bytes = recv(new_fd, received_request, max_len - 1, 0);
    if (num_bytes <= 0) {
        close(new_fd);
        return NULL;
    }

    // request line looks like: "POST /transactions?x=1 HTTP/1.1"
    char method[8] = {0}, target[512] = {0};
    sscanf(received_request, "%7s %511s", method, target);
    char *query = strchr(target, '?');
    if (query) *query++ = '\0';
    char *body = strstr(received_request, "\r\n\r\n");
    if (body) body += 4;

    if (handle_chain_route(new_fd, method, target, query, body)) {
        printf("Handled %s %s\n", method, target);
    }
    //GET request, send a response
    else if (strncmp(received_request, "GET", 3) == 0){
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
        printf("Received Non-GET request. Ignoring ....\n");
        send_response(new_fd, "405 Method Not Allowed", "application/json", "{\"error\":\"method not allowed\"}");
    }

    close(new_fd);
    return NULL;
}

int main() {
    struct addrinfo hints, *res;
    int sockfd;

    chain_init(); // mines the genesis block
    printf("Blockchain ready (difficulty %d)\n", DIFFICULTY);

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

    // lets the server restart right away instead of failing on a port stuck in TIME_WAIT
    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    int bind_result = bind(sockfd, res->ai_addr, res->ai_addrlen);
    // ties socket to an address / port
    printf("Bind result is: %d\n", bind_result);
    if (bind_result < 0) {
        perror("Bind error");
        exit(1);
    }

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
        if (new_fd < 0) {
            perror("Accept error");
            continue;
        }

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = new_fd;

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, fd_ptr);
        pthread_detach(thread);
    }
    close(sockfd);
}