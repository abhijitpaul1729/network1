//2024EB01251_SGA1

#define _POSIX_C_SOURCE 200112L   

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>

#define HTTP_PORT        "80"
#define BUFFER_SIZE      4096
#define RECV_TIMEOUT_SEC 30       

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0            
#endif

int create_socket_and_connect(const char *hostname) {
    // This function creates a socket and connects with hostname
    struct addrinfo hints, *result, *rp;
    char host[256];
    const char *name = hostname;
    size_t len = strlen(hostname);
    int sockfd = -1;
    int last_errno = 0;
    int status;

    
    if (len > 2 && hostname[0] == '[' && hostname[len - 1] == ']' && len - 2 < sizeof(host)) {
        memcpy(host, hostname + 1, len - 2);
        host[len - 2] = '\0';
        name = host;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      
    hints.ai_socktype = SOCK_STREAM;  

    // Works for hostnames and IP addresses both
    status = getaddrinfo(name, HTTP_PORT, &hints, &result);
    if (status != 0) {
        fprintf(stderr, "Cannot resolve %s: %s\n", hostname, gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    // Try each address 
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            last_errno = errno;
            continue;
        }
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;                    // connected
        last_errno = errno;
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(result);

    if (sockfd < 0) {
        fprintf(stderr, "Cannot connect to %s: %s\n", hostname, strerror(last_errno));
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

void send_http_get_request(int sockfd, const char *hostname, const char *path) {
    // This function sends an HTTP GET request for hostname
    
    char request[BUFFER_SIZE];
    size_t sent = 0;
    int len;

    
    if (path == NULL || path[0] == '\0')
        path = "/";
    const char *slash = (path[0] == '/') ? "" : "/";

    // An IPv6 address 
    int ipv6 = (strchr(hostname, ':') != NULL && hostname[0] != '[');
    const char *lb = ipv6 ? "[" : "";
    const char *rb = ipv6 ? "]" : "";

    len = snprintf(request, sizeof(request),
                   "GET %s%s HTTP/1.1\r\n"
                   "Host: %s%s%s\r\n"
                   "User-Agent: simple-http-client/1.0\r\n"
                   "Accept: */*\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   slash, path, lb, hostname, rb);
    if (len < 0 || (size_t)len >= sizeof(request)) {
        fprintf(stderr, "Request is too long\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // send() the data
    while (sent < (size_t)len) {
        ssize_t n = send(sockfd, request + sent, (size_t)len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;            
            perror("send");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        if (n == 0) {                 // no progress: stop instead of looping forever
            fprintf(stderr, "send: connection closed while sending the request\n");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        sent += (size_t)n;
    }

    printf("HTTP GET request sent to %s\n", hostname);
}

void receive_http_response(int sockfd) {
    // This function receives and prints the HTTP response 
    char buffer[BUFFER_SIZE];
    struct timeval tv;
    ssize_t n;

    // Do not wait forever if the server goes silent
    tv.tv_sec = RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        perror("setsockopt");

    // "Connection: close" makes the server close the socket after the response,
    
    for (;;) {
        n = recv(sockfd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            fwrite(buffer, 1, (size_t)n, stdout);
        } else if (n == 0) {
            break;                    
        } else if (errno == EINTR) {
            continue;                 
        } else {
            fflush(stdout);           // show what we got before the error text
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                fprintf(stderr, "\nTimed out: no data from the server for %d seconds. "
                                "The response may be incomplete.\n", RECV_TIMEOUT_SEC);
            else
                perror("recv");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    fflush(stdout);
}

void interact_with_server(const char *hostname, const char *path) {
  
    printf("Interacting with server %s...\n", hostname);

    // Call function to create a socket and connect with server
    int sockfd = create_socket_and_connect(hostname);

    // Call function to send an HTTP GET request
    send_http_get_request(sockfd, hostname, path);

    // Call function to receive and print the HTTP response
    receive_http_response(sockfd);

    // Call function to close the socket
    close(sockfd);
}

int main(int argc, char *argv[]) {
    // This is the main() function that calls other functions and interacts
  
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <hostname or IP address> [path]\n", argv[0]);
        return 1;
    }

    const char *hostname = argv[1];
    const char *path = (argc == 3) ? argv[2] : "/";

    // Call function to process with the server
    interact_with_server(hostname, path);

    return 0;
}
