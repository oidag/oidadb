#ifndef error_h_
#define error_h_

void error(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

#endif