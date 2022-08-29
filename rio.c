/*********************************************************************
 * Robust i/o
 * originally come from textbook csapp and unp
 * https://book.douban.com/subject/5333562/
 * https://book.douban.com/subject/26434583/
 ********************************************************************/
#include "./inc/rio.h"

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
    cnt = (int)n > rp->unread ? rp->unread : n;
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