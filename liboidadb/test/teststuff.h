#ifndef teststuff
#define teststuff
#define EDB_FUCKUPS

#include <oidadb/oidadb.h>

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

extern odb_err err;
extern int test_waserror;
#define stdlogthing(stream,prefix) { \
	int terr = errno; \
va_list args; \
va_start(args, fmt); \
fprintf(stream, prefix ": ");\
vfprintf(stream, fmt, args); \
fprintf(stream, "\n");               \
if(err) {                            \
fprintf(stream, "had odb_err: %d (%s)\n", err, odb_errstr(err));\
}\
if(terr) { \
errno = terr;                        \
fprintf(stream, "had err: %d (%s)\n", terr, strerror(terr)); \
} \
va_end(args);\
errno = terr;}

// if test_error is called, main() will return 1.
__attribute__ ((format (printf, 1, 2)))
static void test_error(const char *fmt, ...)  {
	stdlogthing(stderr, "test-stderr")
	test_waserror = 1;
}

#define test_log(fmt, ...) _test_log (__FILE__, __LINE__, fmt, ##__VA_ARGS__)

__attribute__ ((format (printf, 3, 4)))
static void _test_log(const char *file, unsigned int line, const char *fmt, ...)  {

	// trim the filename.

	fprintf(stdout, "%s:%d: ", file, line);
	stdlogthing(stdout, "test-stdout")
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

// must call test_mkdir first.
// generates a file name, make sure it hasn't already been created.
// see test_filename after.
extern char test_filenmae[100];
static int test_mkfile(const char *argv0) {
	sprintf(test_filenmae, "%s.oidadb", argv0);
	unlink(test_filenmae);
}

// we define main out here.
void test_main();


#endif