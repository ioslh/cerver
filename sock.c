#include "./inc/sock.h"

int open_listenfd(int port) {
  int listenfd, optval = 1;
  struct sockaddr_in server;
  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) fatal_exit(1, "Failed create socket");
  bzero((char *)&server, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port = htons((unsigned short)port);
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int)) < 0)
    fatal_exit(1, "Failed setsockopt");
  if (bind(listenfd, (SA *)&server, sizeof(server)) < 0) fatal_exit(2, "Failed bind socket");
  if (listen(listenfd, LISTENQ) < 0) fatal_exit(3, "Failed listen");
  return listenfd;
}