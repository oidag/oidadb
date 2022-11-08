#ifndef _edbERRORS_H_
#define _edbERRORS_H_ 1

// with crits, if you have an errno to complain about make sure you don't purge it
// as log_crit will look at errno and log what ever it is at the time.
//
// log_critf will preserve errno.
void log_crit(const char *log);
void log_critf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_error(const char *log);
void log_errorf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_noticef(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_info(const char *log);
void log_infof(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_warn(const char *log);
void log_warnf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_debug(const char *log);
void log_debugf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void implementme();

#endif