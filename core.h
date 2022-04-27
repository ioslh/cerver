#ifndef core_h
#define core_h
#include <stdio.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#define NTHREADS 8
#define MAX_HTTP_BUF 2048
#define MAX_PATH_LEN 512
typedef struct {
  int port;
  char *public;
} server_t;
void run_server(int, char *);
void *thread_handle(void *);
#endif /* core_h */
