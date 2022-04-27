#include "utils.h"

// Fatal error happened, should quit server
void fatal(int code, char *msg) {
    fprintf(stderr, "[error]: %s\n", msg ? msg : "Unknown error");
    exit(code);
}