#include "./inc/core.h"

server_t svr;

void run_server(int port, char *public) {
  int listenfd, connfd, i;
  struct sockaddr_in client;
  pthread_t tid;
  sbuf_t sbuf;
  int clientlen = sizeof(client);
  svr.port = port;
  svr.www = public;
  sbuf_init(&sbuf, NTHREADS);
  listenfd = open_listenfd(port);
  for(i = 0; i < NTHREADS; i++) {
    if (pthread_create(&tid, NULL, thread_handle, &sbuf) != 0) fatal_exit(3, "Failed create thread");
  }

  printf("Cerver start on port %d...\n", port);
  while(1) {
    if ((connfd = accept(listenfd, (SA *)&client, (socklen_t *)&clientlen)) < 0) fatal_exit(3, "Failed accept connection");
    report_client(&client);
    sbuf_insert(&sbuf, connfd);
  }
}


void *thread_handle(void *arg) {
  sbuf_t *sp = (sbuf_t *)arg;
  int connfd;
  if (pthread_detach(pthread_self()) != 0) fatal_exit(2, "Failed detach thread");
  while(1) {
      connfd = sbuf_delete(sp);
      web_handle(&svr, connfd);
      printf("Close connection from connection %d\n", connfd);
      if (close(connfd) < 0) fatal_exit(4, "Failed close connection");
  }
}

void report_client(struct sockaddr_in *client) {
    char *ip = inet_ntoa(client->sin_addr);
    printf("Connected from %s\n", ip);
}