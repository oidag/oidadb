#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "errors.h"

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

void log_crit(const char *log) {
	log_critf("%s", log);
}
void log_critf(const char *fmt, ...) {
	stdlogthing(stderr, "crit");
}

void log_error(const char *log) {
	log_errorf("%s", log);
}
void log_errorf(const char *fmt, ...) {
	stdlogthing(stderr, "error");
}

void log_debug(const char *log) {
	log_debugf("%s", log);
}
void log_debugf(const char *fmt, ...)  {
	stdlogthing(stdout, "debug");
}

void log_noticef(const char *fmt, ...) {
	stdlogthing(stdout, "notice");
}

void log_info(const char *log) {
	log_infof("%s", log);
}
void log_infof(const char *fmt, ...) {
	stdlogthing(stdout, "info");
}