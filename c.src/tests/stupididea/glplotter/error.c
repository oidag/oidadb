#include "error.h"

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#define stdlogthing(stream,prefix) { \
	int terr = errno; \
va_list args; \
va_start(args, fmt); \
fprintf(stream, prefix ": ");\
vfprintf(stream, fmt, args); \
fprintf(stream, "\n"); \
if(terr) { \
errno = terr; \
perror(prefix "had errno"); \
} \
va_end(args);\
errno = terr;}

void error(const char *fmt, ...) {
	stdlogthing(stderr, "error");
}