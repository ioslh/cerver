/*
 * Cerver: A simple HTTP server that serve static files
 * 2022/4/16 @ioslh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
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

struct Header {
    char *name;
    char *value;
    struct Header *next;
};
typedef struct Header header_t;


struct Request {
    // longest methods are `options` and `connect`, 8 bytes is ok
    char method[8];
    header_t *header;
    char *url;
};
typedef struct Request req_t;

struct Response {
    char *body;
    size_t length;
    header_t *header;
    // remember last header, thus don't have to search entire linked list for appending
    header_t *last;
    // keep int last, minimize memory while satisfy align requirement on 64-bit
    int status;
};
typedef struct Response res_t;

typedef struct {
    int fd;             // Binded file descriptor
    int unread;         // unread data in buffer
    char *cursor;       // read cursor(AKA file position)
    char buf[BUF_MAX];   // internal buffer
} rio_t;
typedef struct sockaddr SA;
ssize_t rio_read(rio_t *, char *, size_t);
ssize_t rio_readline(rio_t *, void *, size_t);
ssize_t rio_writen(int, char *, size_t);
void rio_init(rio_t *, int);
void fatal(int, char *);
void web_handle(int);
void report_client(struct sockaddr_in *);
int read_startline(rio_t *, req_t *, res_t *);
int read_request_headers(rio_t *, req_t *, res_t *);
int trimright_line(char *);
int get_request(rio_t *, req_t *, res_t *);
int handle_request(req_t *, res_t *);
void free_request(req_t *);
void print_headers(header_t *);
void free_response(res_t *);
void free_headers(header_t *header);
char *get_http_message(int);

header_t *new_header(const char *, const char *);
void httpsend_error(int connfd, res_t *res);
void httpsend(int connfd, res_t *res);
void req_init(req_t *);
void res_init(res_t *);

// Public dir
char *public;

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
    int listenfd, connfd;
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) fatal(1, "Failed create socket");
    bzero((char *)&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenfd, (SA *)&server, sizeof(server)) < 0) fatal(2, "Failed bind socket");
    if (listen(listenfd, 1024) < 0) fatal(3, "Failed listen");
    printf("Cerver start on port %hd\n", port);
    while(1) {
        if ((connfd = accept(listenfd, (SA *)&client, (socklen_t *)&clientlen)) < 0) fatal(3, "Failed accept connection");
        report_client(&client);
        web_handle(connfd);
        printf("Close connection %d\n", connfd);
        if (close(connfd) < 0) fatal(4, "Failed close connection");
    }
}

/*********************************************************************
 * Robust i/o
 * originally come from textbook csapp and unp
 * https://book.douban.com/subject/5333562/
 * https://book.douban.com/subject/26434583/
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
// =========================== End of rio =============================

// Print usage guide
void usage(char *binname) {
    printf("\nUsage: %s <port>\n\n", binname);
    exit(0);
}


// Fatal error happened, should quit server
void fatal(int code, char *msg) {
    fprintf(stderr, "[error]: %s\n", msg ? msg : "Unknown error");
    exit(code);
}

int read_startline(rio_t *rp, req_t *req, res_t *res) {
    char line[HDR_MAX];
    char url[URI_MAX];
    if (rio_readline(rp, line, HDR_MAX) < 0) {
        res->status = 400;
        return -1;
    }
    trimright_line(line);
    if ((sscanf(line, "%7s %s %*s", req->method, url)) != 2) {
        res->status = 400;
        return -1;
    }
    if (strncasecmp(req->method, "GET", 3) != 0) {
        res->status = 405;
        return -1;
    }
    size_t urllen = strlen(url);
    req->url = (char *)malloc(urllen + 1);
    strncpy(req->url, url, urllen + 1);
    return 0;
}

int read_request_headers(rio_t *rp, req_t *req, res_t *res) {
    char line[HDR_MAX];
    size_t n;
    char *nameptr, *valueptr, *colonptr;
    header_t *prev_header = NULL, *temp_header = NULL;
    while((n = rio_readline(rp, line, HDR_MAX)) > 0) {
        n -= trimright_line(line);
        if (n == 0) break; // empty line, end of headers
        // if less than 3, we can surely know that's not a valid header
        if (n < 3) continue;

        nameptr = line;
        if ((colonptr = strnstr(line, ":", n - 1)) == NULL) {
            continue; // invalid header line
        }
        *colonptr = 0;
        valueptr = colonptr + 1;
        while(isspace(*valueptr)) {
            valueptr++;
            if (*valueptr == 0) break;
        }
        temp_header = new_header(nameptr, valueptr);
        if (prev_header) {
            prev_header->next = temp_header;
        } else {
            req->header = temp_header;
        }
        prev_header = temp_header;
    }
    return 0;
}

int trimright_line(char *line) {
    size_t len = strlen(line);
    if (len == 0) return 0;
    // both CRLF and CR are valid as line terminator
    if (line[len - 1] == 10) {
        line[len - 1] = 0;
        if (len > 1 && line[len - 2] == 13) {
            line[len - 2] = 0;
            return 2;
        }
        return 1;
    }
    return 0;
}

int handle_request(req_t *req, res_t *res) {
    // serve static file
    char filename[URI_MAX];
    strcpy(filename, public);
    strcat(filename, req->url);
    struct stat st;
    char *pos;
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
        res->status = 404;
        return -1;
    }
    int fd = open(filename, O_RDONLY, 0);
    char *bodybuf = (char *)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    res->body = (char *)malloc(st.st_size);
    strncpy(res->body, bodybuf, st.st_size);
    res->length = st.st_size;
    res->status = 200;
    close(fd);
    munmap(bodybuf, st.st_size);
    return 0;
}

void web_handle(int connfd) {
    rio_t rio;
    req_t req;
    res_t res;
    rio_init(&rio, connfd);
    req_init(&req);
    res_init(&res);
    if (read_startline(&rio, &req, &res) == 0) {
        if (read_request_headers(&rio, &req, &res) == 0) {
            if (handle_request(&req, &res) == 0) {
                httpsend(connfd, &res);
            } else {
                httpsend_error(connfd, &res);
            }
        } else {
            httpsend_error(connfd, &res);
        }
    } else {
        httpsend_error(connfd, &res);
    }
    free_request(&req);
    free_response(&res);
}


void httpsend_error(int connfd, res_t *res) {
    char bodybuf[BUF_MAX];
    char *message = get_http_message(res->status);
    sprintf(bodybuf, "<html>");
    sprintf(bodybuf, "%s  <head><title>%d %s</title></head>", bodybuf, res->status, message);
    sprintf(bodybuf, "%s  <body>", bodybuf);
    sprintf(bodybuf, "%s    <h3>%d %s</h3>", bodybuf, res->status, message);
    sprintf(bodybuf, "%s  </body></html>", bodybuf);
    size_t bodylen = strlen(bodybuf);
    char *body = (char *)malloc(bodylen + 1);
    strncpy(body, bodybuf, bodylen + 1);
    res->body = body;
    res->length = bodylen;
    httpsend(connfd, res);
}

void append_header(res_t *res, header_t *header) {
    if (res->last) {
        res->last->next = header;
        res->last = header;
    } else {
        res->header = res->last = header;
    }

}

void httpsend(int connfd, res_t *res) {
    char valuebuf[16];
    char headbuf[BUF_MAX];
    header_t *header = NULL;
    sprintf(valuebuf, "%ld", res->length);
    append_header(res, new_header("Server", SERVER_NAME));
    append_header(res, new_header("Date", "xxx"));
    append_header(res, new_header("Content-Length", valuebuf));
    sprintf(headbuf, "HTTP/1.1 %d %s%s", res->status, get_http_message(res->status), CRLF);
    header = res->header;

    while(header) {
        sprintf(headbuf, "%s%s: %s%s", headbuf, header->name, header->value, CRLF);
        header = header->next;
    }

    sprintf(headbuf, "%s%s", headbuf, CRLF);
    rio_writen(connfd, headbuf, strlen(headbuf));
    rio_writen(connfd, res->body, res->length);
}

void report_client(struct sockaddr_in *client) {
    char *ip = inet_ntoa(client->sin_addr);
    printf("Connected from %s\n", ip);
}

header_t * new_header(const char *name, const char *value) {
    header_t *header = (header_t *)malloc(sizeof(header_t));
    size_t namelen = strlen(name), valuelen = strlen(value);
    char *nameptr = (char *)malloc(namelen + 1);
    char *valueptr = (char *)malloc(valuelen + 1);
    if (!header || !nameptr || !valueptr) fatal(1, "Failed allocate memory for header");
    strncpy(nameptr, name, namelen + 1);
    strncpy(valueptr, value, valuelen + 1);
    header->next = NULL;
    header->name = nameptr;
    header->value = valueptr;
    return header;
}

void print_headers(header_t *header) {
    while(header) {
        printf("%s: %s\n", header->name, header->value);
        header = header->next;
    }
}

// These would be enough for now
char *get_http_message(int code) {
    // split handle different code ranges, thus minimize jump table
    if (code < 300) {
        switch(code) {
            case 200:
                return "OK";
            case 204:
                return "No Content";
        }
    }

    if (code < 500) {
        switch(code) {
            case 400:
                return "Bad Request";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 405:
                return "Method Not Allowed";
        }
    }
    if (code == 500) return "Internal Server Error";
    return "";
}

void free_headers(header_t *header) {
    header_t *curr = header, *next;
    while(curr) {
        next = curr->next;
        free(curr->name);
        free(curr->value);
        free(curr);
        curr = next;
    }
}

void free_request(req_t *req) {
    if (req->url) free(req->url);
    if (req->header) free_headers(req->header);
}

void free_response(res_t *res) {
    if (res->body) free(res->body);
    if (res->header) free_headers(res->header);
}

void req_init(req_t *req) {
    req->header = NULL;
    req->url = NULL;
}

void res_init(res_t *res) {
    res->body = NULL;
    res->length = 0;
    res->header = NULL;
    res->last = NULL;
    res->status = 0;
}