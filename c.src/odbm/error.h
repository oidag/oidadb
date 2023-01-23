#ifndef error_h_
#define error_h_

void log_error(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

void log_verbose(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
#endif