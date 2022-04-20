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
#include <time.h>
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
// According to Posix style convention, -1 failed, 0 ok
#define FAILED -1
#define OK 0

typedef struct Location {
    char *hash;
    char *path;
    char *query;
} location_t;
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
    location_t *location;
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
void append_header(res_t *, header_t *);
void free_response(res_t *);
void free_headers(header_t *header);
char *get_http_message(int);

header_t *new_header(const char *, const char *);
void httpsend_error(int connfd, res_t *res);
void httpsend(int connfd, res_t *res);
void req_init(req_t *);
void res_init(res_t *);
char *stringify_time(time_t);
char *get_extension(const char *);
char *get_mime(const char *);
int parse_location(char *, location_t*);
int check_method(char *);
void child_handler(int);

// Public dir
char *public;
char *mime_table[][2] = {
    {"html", "text/html"},
    {"css", "text/css"},
    {"js", "application/javascript"},
    {"png", "image/png"},
};
// Supported methods
char *methods[] = {
    "GET",
    "HEAD",
};

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
    signal(SIGCHLD, child_handler);
    printf("Cerver start on port %hd...\n", port);
    pid_t pid;
    while(1) {
        if ((connfd = accept(listenfd, (SA *)&client, (socklen_t *)&clientlen)) < 0) fatal(3, "Failed accept connection");
        report_client(&client);
        pid = fork();
        if (pid < 0) fatal(4, "Failed fork child process");
        if (pid == 0) {
            web_handle(connfd);
            printf("Close connection from child process %d\n", connfd);
            if (close(connfd) < 0) fatal(4, "Failed close connection");
            exit(0);
        }
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
        return FAILED;
    }
    trimright_line(line);
    if ((sscanf(line, "%7s %s %*s", req->method, url)) != 2) {
        res->status = 400;
        return FAILED;
    }
    if (strlen(url) == 0 || url[0] != '/') {
        // Only support abs path, cannot be empty, MUST start with '/'
        // https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html#sec5.1.2
        res->status = 400;
        return FAILED;
    }
    req->location = (location_t *)malloc(sizeof(location_t));
    parse_location(url, req->location);

    if (check_method(req->method) != OK) {
        res->status = 405;
        return FAILED;
    }

    return OK;
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
    return OK;
}

int trimright_line(char *line) {
    size_t len = strlen(line);
    if (len == 0) return 0;
    // both CRLF and LF are valid as line terminator
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
    strcat(filename, req->location->path);
    struct stat st;
    char *pos, *mime;
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
        return FAILED;
    }

    res->status = 200;
    append_header(res, new_header("Last-Modified", stringify_time(st.st_mtime)));
    mime = get_mime(get_extension(filename));
    if (mime) append_header(res, new_header("Content-Type", mime));
    if (strncasecmp(req->method, "HEAD", 4) == 0) {
        res->body = NULL;
        res->length = 0;
        return OK;
    }
    int fd = open(filename, O_RDONLY, 0);
    char *bodybuf = (char *)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    res->body = (char *)malloc(st.st_size);
    memcpy(res->body, bodybuf, st.st_size);
    res->length = st.st_size;
    close(fd);
    munmap(bodybuf, st.st_size);
    return OK;
}

void web_handle(int connfd) {
    rio_t rio;
    req_t req;
    res_t res;
    rio_init(&rio, connfd);
    req_init(&req);
    res_init(&res);
    if (read_startline(&rio, &req, &res) == OK) {
        if (read_request_headers(&rio, &req, &res) == OK) {
            if (handle_request(&req, &res) == OK) {
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
    append_header(res, new_header("Content-Type", "text/html"));
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
    append_header(res, new_header("Date", stringify_time(time(NULL))));
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
    if (req->location) {
        if (req->location->path) free(req->location->path);
        if (req->location->hash) free(req->location->hash);
        if (req->location->query) free(req->location->query);
        free(req->location);
    }
    if (req->header) free_headers(req->header);
}

void free_response(res_t *res) {
    if (res->body) free(res->body);
    if (res->header) free_headers(res->header);
}

void req_init(req_t *req) {
    req->header = NULL;
    req->location = NULL;
}

void res_init(res_t *res) {
    res->body = NULL;
    res->length = 0;
    res->header = NULL;
    res->last = NULL;
    res->status = 0;
}

char *stringify_time(time_t t) {
    char *ret = ctime(&t);
    trimright_line(ret);
    return ret;
}

char *get_extension(const char *name) {
    char *ret = rindex(name, '.');
    return ret ? ret + 1 : NULL;
}

char *get_mime(const char *ext) {
    if (!ext) return NULL;
    size_t len = sizeof(mime_table) / sizeof(mime_table[0]), i;
    for(i = 0; i < len; i++) {
        if (strncasecmp(mime_table[i][0], ext, strlen(ext)) == 0) {
            return mime_table[i][1];
        }
    }
    return NULL;
}


int parse_location(char *url, location_t* loc) {
    char *queryptr, *hashptr;
    size_t len = 0;
    loc->path = NULL;
    loc->hash = NULL;
    loc->query = NULL;
    if ((queryptr = index(url, '?')) != NULL) {
        *queryptr = 0;
        queryptr++;
    }
    if ((hashptr = index(url, '#')) != NULL) {
        *hashptr = 0;
        hashptr++;
    }
    len = strlen(url) + 1;
    loc->path = (char *)malloc(len);
    strncpy(loc->path, url, len);

    if (queryptr && (len = strlen(queryptr)) > 0) {
        loc->query = (char *)malloc(len);
        strncpy(loc->query, queryptr, len);
    }

    if (hashptr && (len = strlen(hashptr)) > 0) {
        loc->hash = (char *)malloc(len);
        strncpy(loc->hash, hashptr, len);
    }
    return OK;
}

int check_method(char *method) {
    size_t i, len = sizeof(methods) / sizeof(methods[0]);
    for(i = 0; i < len; i++) {
        if (strncasecmp(methods[i], method, strlen(method)) == 0) {
            return OK;
        }
    }
    return FAILED;
}


void child_handler(int sig) {
    pid_t pid;
    int status;
    while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        //
        printf("Child process %d reaped\n", pid);
    }
}