#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "errors.h"

void log_crit(const char *log) {
	log_critf("%s", log);
}
void log_critf(const char *fmt, ...) {
	int terr = errno;
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "log_critf:");
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	if(terr) {
		errno = terr;
		perror("log_critf had errno");
	}
	va_end(args);
	errno = terr;
}

void log_error(const char *log) {
	log_errorf("%s", log);
}
void log_errorf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
}

void log_debug(const char *log) {
	log_debugf("%s", log);
}
void log_debugf(const char *fmt, ...)  {
	va_list args;
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	fprintf(stdout, "\n");
	va_end(args);
}