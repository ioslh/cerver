/*********************************************************************
 * Robust i/o
 * originally come from textbook csapp and unp
 * https://book.douban.com/subject/5333562/
 * https://book.douban.com/subject/26434583/
 ********************************************************************/
#ifndef rio_h
#define rio_h
#include <string.h>
#include <errno.h>
#include <unistd.h>
#define IO_BUF_MAX 2048
typedef struct {
    int fd;             // Binded file descriptor
    int unread;         // unread data in buffer
    char *cursor;       // read cursor(AKA file position)
    char buf[IO_BUF_MAX];   // internal buffer
} rio_t;

void rio_init(rio_t *, int);
ssize_t rio_read(rio_t *, char *, size_t);
ssize_t rio_readline(rio_t *, void *, size_t);
ssize_t rio_writen(int, char *, size_t);
#endif /* rio_h */
