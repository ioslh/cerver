#ifndef core_h
#define core_h
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "sock.h"
#include "sbuf.h"
#include "http.h"
#include "utils.h"

#define NTHREADS 8
#define MAX_HTTP_BUF 2048
#define MAX_PATH_LEN 512

void run_server(int, char *);
void *thread_handle(void *);

#endif /* core_h */
