#include "./inc/core.h"

int main(int argc, char **argv) {
  short port = 0;
  if (argc < 2 || sscanf(argv[1], "%hd", &port) != 1) {
      // use default 8080 port
      port = 8080;
  }
  char *public;
  if ((public = getcwd(NULL, MAX_PATH_LEN)) == NULL) {
      fatal_exit(1, "Failed reading public dir"); 
  }
  run_server(port, public);
  return 0;
}