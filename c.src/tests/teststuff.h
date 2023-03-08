#ifndef teststuff
#define teststuff
#define EDB_FUCKUPS

#include "oidadb.h"

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <sys/time.h>

edb_err err;
static int test_waserror = 0;
#define stdlogthing(stream,prefix) { \
	int terr = errno; \
va_list args; \
va_start(args, fmt); \
fprintf(stream, prefix ": ");\
vfprintf(stream, fmt, args); \
fprintf(stream, "\n");               \
if(err) {                            \
fprintf(stream, "had edb_err: %d\n", err);\
}\
if(terr) { \
errno = terr; \
perror(prefix "had errno"); \
} \
va_end(args);\
errno = terr;}

__attribute__ ((format (printf, 1, 2)))
static void test_error(const char *fmt, ...)  {
	stdlogthing(stderr, "test")
	test_waserror = 1;
}
static void test_log(const char *fmt, ...)  {
	stdlogthing(stdout, "test")
}


typedef uint64_t timer;
typedef uint64_t timepassed;

// returns a timer to be used with timerend()
timer static timerstart() {
	struct timeval tv;
	gettimeofday(&tv,0);
	return (uint64_t)tv.tv_sec*1000000 + tv.tv_usec;
}
// returns nanoseconds that passed between start and end.
timepassed static timerend(timer t) {
	struct timeval tv;
	gettimeofday(&tv,0);
	uint64_t finisht = (uint64_t)tv.tv_sec*1000000 + tv.tv_usec;
	return finisht - t;
}
double static timetoseconds(timepassed t) {
	return (double)t/1000000;
}


// creates build/tests if it wasn't already created.
// it will then cd into build/tests for easyness
static int test_mkdir() {
	char buff[200];
	getcwd(buff, 200);
	const char *dir = "build/test";
	if(strcmp(basename(buff), "build") == 0) {
		// we're already in build folder
		dir = "test";
	}
	int err = mkdir(dir, 0777);
	if(err == -1 && errno != EEXIST) {
		test_error("failed to make build/tests");
		return 1;
	}
	errno = 0; // clear out errno
	err = chdir(dir);
	if(err) {
		test_error("failed to chdir into build/tests");
		return 1;
	}
	return 0;
}

// must call test_mkdir first.
// generates a file name, make sure it hasn't already been created.
// see test_filename after.
char test_filenmae[100];
static int test_mkfile(const char *argv0) {
	sprintf(test_filenmae, "%s.oidadb", argv0);
	unlink(test_filenmae);
}

#endif