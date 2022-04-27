#ifndef sock_h
#define sock_h
#include <strings.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#define LISTENQ 1024

typedef struct sockaddr SA;
int open_listenfd(int);

#endif /* sock_h */
