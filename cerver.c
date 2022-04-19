/*
 * A Simple Http server
 * 2022/4/16 @ioslh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#define MAXBUF 2048
#define MAXURI 1024
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define LINESEP "\r\n"
#define SERVER_NAME "tiny web server"

typedef struct {
    int fd;             // Binded file descriptor
    int unread;         // unread data in buffer
    char *cursor;       // read cursor(AKA file position)
    char buf[MAXBUF];   // internal buffer
} rio_t;
typedef struct sockaddr SA;
ssize_t rio_read(rio_t *, char *, size_t);
ssize_t rio_readline(rio_t *, void *, size_t);
ssize_t rio_writen(int, char *, size_t);
void rio_init(rio_t *, int);
void handle(int);
void error_page(int, int, char *);
void report_client(struct sockaddr_in *);

char *public = "/Users/slh/Temp/test-html";

void usage(char *binname) {
    printf("\nUsage: %s <port>\n\n", binname);
    exit(0);
}

void report(int code, char *msg) {
    fprintf(stderr, "[error]: %s\n", msg ? msg : "Unknown error");
    exit(code);
}

int main(int argc, char **argv, char **envptr) {
    short port = 0;
    if (argc < 2 || sscanf(argv[1], "%hd", &port) != 1) {
        // use default 8080 port
        port = 8080;
        printf("Using default 8080 port\n");
    }
    struct sockaddr_in client, server;
    int clientlen = sizeof(client);
    int listenfd, connfd;
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) report(1, "Failed create socket");
    bzero((char *)&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenfd, (SA *)&server, sizeof(server)) < 0) report(2, "Failed bind socket");
    if (listen(listenfd, 1024) < 0) report(3, "Failed listen");
    while(1) {
        if ((connfd = accept(listenfd, (SA *)&client, (socklen_t *)&clientlen)) < 0) report(3, "Failed accept connection");
        report_client(&client);
        handle(connfd);
        if (close(connfd) < 0) report(4, "Failed close connection");
    }
}

/*********************************************************************
 * Robust i/o
 * originally come from textbook csapp and unp
 ********************************************************************/

void rio_init(rio_t *rp, int fd) {
    rp->fd = fd;
    rp->cursor = rp->buf;
    rp->unread = 0;
}

ssize_t rio_read(rio_t *rp, char *buf, size_t n) {
    int cnt;
    while(rp->unread <= 0) {
        rp->unread = read(rp->fd, rp->buf, sizeof(rp->buf));
        if (rp->unread < 0) {
            if (errno == EINTR) {
                // interrupted by signal
                // should restart read
                // do nothing, loop re-execute
            } else {
                // Bad things happened, just return
                return -1;
            }
        } else if (rp->unread == 0) {
            // EOF
            return 0;
        } else {
            // read some data as expected
            rp->cursor = rp->buf;
        }
    }
    cnt = MIN(n, rp->unread);
    memcpy(buf, rp->cursor, cnt);
    rp->cursor += cnt;
    rp->unread -= cnt;
    return cnt;
}

// read rp internal buffer byte-by-byte until found LF or EOF or reached maxlen
ssize_t rio_readline(rio_t *rp, void *usrbuf, size_t maxlen) {
    int t;
    size_t i;
    char ch;
    char *bufp = usrbuf;
    for(i = 0; i < maxlen; i++) {
       if ((t = rio_read(rp, &ch, 1)) > 0) {
           *bufp++ = ch;
           if (ch == '\n') {
               // CSAPP textbook e2 is buggy here while e3 fixed this
               i++;
               break;
           }
       } else if (t == 0) {
           // EOF reached
           if (i == 0) {
               // read nothing
               return 0;
           } else {
               break;
           }
       } else {
           return -1;
       }
    }
    *bufp = 0;
    return i;
}

ssize_t rio_writen(int fd, char *buf, size_t n) {
    int nwrite, nleft = n;
    char *ptr = buf;
    
    while(nleft) {
        nwrite = write(fd, ptr, nleft);
        if (nwrite < 0) {
            if (errno == EINTR) {
                // do nothing, loop re-execute
                nwrite = 0;
            } else {
                return -1;
            }
        }
        nleft -= nwrite;
        ptr += nwrite;
    }
    return n;
}

void handle(int connfd) {
    rio_t rio;
    rio_init(&rio, connfd);
    char header[MAXBUF];
    if (rio_readline(&rio, header, MAXBUF) < 0) {
        error_page(connfd, 400, "Bad request");
        return;
    }
    char method[8]; // longest method is `options` and `connect`
    char uri[MAXBUF];
    char version[9];
    if ((sscanf(header, "%8s %s %9s", method, uri, version)) != 3) {
        error_page(connfd, 400, "Bad request");
        return;
    }
    printf("Method: %s, Uri: %s, Version: %s\n", method, uri, version);
    if (strncasecmp(method, "GET", 3) != 0) {
        error_page(connfd, 405, "Method not allowed");
        return;
    }
    // serve static file
    char filename[MAXURI];
    strcpy(filename, public);
    strcat(filename, uri);
    struct stat st;
    char *pos;
    printf("Filename is %s\n", filename);
    if (stat(filename, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
             pos = &filename[strlen(filename) - 1];
             if (*pos != '/') {
                 *(pos + 1) = '/';
                 *(pos + 2) = 0;
             }
             // Default page
             strcat(filename, "index.html");
         }
    }
    if (stat(filename, &st) < 0) {
        error_page(connfd, 404, "Not found");
        return;
    }
    int fd = open(filename, O_RDONLY, 0);
    char *content = (char *)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    char head[MAXBUF];
    sprintf(head, "HTTP/1.1 200 OK%s", LINESEP);
    sprintf(head, "%sServer: %s%s", head, SERVER_NAME, LINESEP);
    sprintf(head, "%sContent-Type: text/html%s", head, LINESEP);
    sprintf(head, "%sContent-Length: %lld%s", head, st.st_size, LINESEP);
    rio_writen(connfd, head, strlen(head));
    rio_writen(connfd, content, st.st_size);
}


void error_page(int fd, int statuscode, char *msg) {
    char *body = (char *)malloc(512);
    char *head = (char *)malloc(256);
    bzero(body, 512);
    bzero(head, 256);

    if (!body || !head) {
        report(5, "Allocate memory error");
    }
    sprintf(body, "<html>");
    sprintf(body, "%s  <head><title>%d %s</title></head>", body, statuscode, msg);
    sprintf(body, "%s  <body>", body);
    sprintf(body, "%s    <h3>%d %s</h3>", body, statuscode, msg);
    sprintf(body, "%s  </body></html>", body);
    
    sprintf(head, "HTTP/1.1 %d %s%s", statuscode, msg, LINESEP);
    sprintf(head, "%sServer: %s%s", head, SERVER_NAME, LINESEP);
    sprintf(head, "%sDate: xxx%s", head, LINESEP);
    sprintf(head, "%sContent-Type: text/html%s", head, LINESEP);
    sprintf(head, "%sContent-Length: %ld%s", head, strlen(body), LINESEP);
    sprintf(head, "%sConnection: close%s%s", head, LINESEP, LINESEP);

    rio_writen(fd, head, strlen(head));
    rio_writen(fd, body, strlen(body));
    free(head);
    free(body);
}

void report_client(struct sockaddr_in *client) {
    //
}


