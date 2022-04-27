#include "utils.h"

void fatal_exit(int code, char *msg) {
  fprintf(stderr, "[fatal] %s\n", msg);
  exit(code);
} 