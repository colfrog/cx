#include <stdbool.h>
#include <sys/types.h>

#ifdef __GLIBC__
#define _BSD_SOURCE
#include <strings.h>
#include <getopt.h>
#include <sys/stat.h>
#endif

#ifndef CX_DIR_NAME
#define CX_DIR_NAME "cx"
#endif

#ifndef CX_DATA_DIR
#define CX_DATA_DIR "~/.local/share"
#endif

#define DEFAULT_SOCKET_NAME "socket"
#define DEFAULT_DATAFILE_NAME "data"
#define DEFAULT_LOCKFILE_NAME "lock"

void cx_set_path(char *, bool, char *, size_t);
