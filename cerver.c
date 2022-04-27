/*
 * Cerver: A simple HTTP server that serve static files
 * 2022/4/16 @ioslh
 */
#include "cerver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#ifndef PATH_MAX
#define PATH_MAX 128
#endif
#define BUF_MAX 2048
/**
 * Although 512 may not enough in real life(eg: IE limit 2083, see link below)
 * but as an experimental project, 512 is happy to go
 * https://support.microsoft.com/en-us/topic/maximum-url-length-is-2-083-characters-in-internet-explorer-174e7c8a-6666-f4e0-6fd6-908b53c12246
 */
#define URI_MAX 512
// Max chars in a single header line
#define HDR_MAX 2048
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define CRLF "\r\n"
#define SERVER_NAME "Cerver"
#define NTHREADS 8


void *thread_handle(void *);



// Public dir
char *public;


sbuf_t sbuf;

int main(int argc, char **argv, char **envptr) {
    short port = 0;
    if (argc < 2 || sscanf(argv[1], "%hd", &port) != 1) {
        // use default 8080 port
        port = 8080;
    }
    if ((public = getcwd(NULL, PATH_MAX)) == NULL) {
       fatal(1, "Failed reading public dir"); 
    }
    struct sockaddr_in client, server;
    int clientlen = sizeof(client);
    int listenfd, connfd, i;
    pthread_t tid;
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) fatal(1, "Failed create socket");
    bzero((char *)&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenfd, (SA *)&server, sizeof(server)) < 0) fatal(2, "Failed bind socket");
    if (listen(listenfd, 1024) < 0) fatal(3, "Failed listen");
    for(i = 0; i < NTHREADS; i++) {
        if (pthread_create(&tid, NULL, thread_handle, NULL) != 0) fatal(3, "Failed create thread");
    }
    sbuf_init(&sbuf, NTHREADS);
    printf("Cerver start on port %hd...\n", port);
    pid_t pid;
    while(1) {
        if ((connfd = accept(listenfd, (SA *)&client, (socklen_t *)&clientlen)) < 0) fatal(3, "Failed accept connection");
        report_client(&client);
        sbuf_insert(&sbuf, connfd);
    }
}

// Print usage guide
void usage(char *binname) {
    printf("\nUsage: %s <port>\n\n", binname);
    exit(0);
}

void *thread_handle(void *arg) {
    int connfd;
    if (pthread_detach(pthread_self()) != 0) fatal(2, "Failed detach thread");

    while(1) {
        connfd = sbuf_delete(&sbuf);
        web_handle(connfd);
        printf("Close connection from child process %d\n", connfd);
        if (close(connfd) < 0) fatal(4, "Failed close connection");
    }
}




