#ifndef _edbERRORS_H_
#define _edbERRORS_H_ 1

#include "options.h"

#define log_critf(fmt, ...) _log_critf(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

// with crits, if you have an errno to complain about make sure you don't purge it
// as log_crit will look at errno and log what ever it is at the time.
//
// log_critf will preserve errno.
//
// log_critf will always return ODB_ECRIT
odb_err _log_critf(const char *file, int line, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));

void log_error(const char *log);
void log_errorf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_noticef(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void log_alertf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_info(const char *log);
void log_infof(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_warn(const char *log);
void log_warnf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_debug(const char *log);
void log_debugf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

// emmits a log_critf() if !assertion, may also crash depending on settings.
#ifdef EDB_FUCKUPS
#define assert(assertion) if(!(assertion)) log_critf("assertion failure")
#else
#define assert(assertion)
#endif

void implementme();

#endif