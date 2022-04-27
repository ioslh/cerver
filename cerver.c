#include "core.h"

int main() {
  char *public = getcwd(NULL, MAX_PATH_LEN);
  int port = 8080;
  run_server(port, public);
  return 0;
}