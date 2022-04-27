#ifndef utils_h
#define utils_h

#include <stdio.h>
#include <stdlib.h>
// According to Posix style convention, -1 failed, 0 ok
#define FAILED -1
#define OK 0
#ifndef PATH_MAX
#define PATH_MAX 128
#endif
#define BUF_MAX 2048
/**
 * Although 512 may not enough in real life(eg: IE limit 2083, see link below)
 * but as an experimental project, 512 is happy to go
 * https://support.microsoft.com/en-us/topic/maximum-url-length-is-2-083-characters-in-internet-explorer-174e7c8a-6666-f4e0-6fd6-908b53c12246
 */
#define URI_MAX 512
// Max chars in a single header line
#define HDR_MAX 2048

void fatal(int, char *);

#endif /* utils_h */
