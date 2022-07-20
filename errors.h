#ifndef _edbERRORS_H_
#define _edbERRORS_H_ 1

void log_crit(const char *log);
void log_critf(const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

void log_error(const char *log);
void log_errorf(const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

void log_info(const char *log);
void log_infof(const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

void log_debug(const char *log);
void log_debugf(const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

#endif