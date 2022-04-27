#ifndef rio_h
#define rio_h

#include <string.h>
#include <unistd.h>
#define BUF_MAX 2048

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

#endif /* rio_h */
