#ifndef handle_h
#define handle_h

#include "rio.h"
#include "utils.h"
#include <fcntl.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/stat.h>

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

#endif /* handle_h */
