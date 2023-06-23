#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "include/oidadb.h"
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
perror(prefix " (had errno)"); \
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

void log_alertf(const char *fmt, ...) {
	stdlogthing(stdout, "notice");
}


void log_info(const char *log) {
	log_infof("%s", log);
}
void log_infof(const char *fmt, ...) {
	stdlogthing(stdout, "info");
}

void log_warnf(const char *fmt, ...) {
	stdlogthing(stdout, "warn");
}

void implementme() {

	// cause a segfaut and div0.
	int *throw = (void *)(0);
	*throw = 1;
}

const char *odb_errstr(odb_err error) {
	switch (error) {
		case ODB_ENONE:     return "ODB_ENONE";
		case ODB_ECRIT:     return "ODB_ECRIT";
		case ODB_EINVAL:    return "ODB_EINVAL";
		case ODB_ENOHANDLE: return "ODB_ENOHANDLE";
		case ODB_EHANDLE:   return "ODB_EHANDLE";
		case ODB_ENOENT:    return "ODB_ENOENT";
		case ODB_EEXIST:    return "ODB_EEXIST";
		case ODB_EEOF:      return "ODB_EEOF";
		case ODB_EFILE:     return "ODB_EFILE";
		case ODB_ENOTDB:    return "ODB_ENOTDB";
		case ODB_EOPEN:     return "ODB_EOPEN";
		case ODB_ECLOSED:   return "ODB_ECLOSED";
		case ODB_ENOHOST:   return "ODB_ENOHOST";
		case ODB_EOUTBOUNDS:return "ODB_EOUTBOUNDS";
		case ODB_EERRNO:    return "ODB_EERRNO";
		case ODB_EHW:       return "ODB_EHW";
		case ODB_ENOMEM:    return "ODB_ENOMEM";
		case ODB_ENOSPACE:  return "ODB_ENOSPACE";
		case ODB_ESTOPPING: return "ODB_ESTOPPING";
		case ODB_EAGAIN:    return "ODB_EAGAIN";
		case ODB_EULOCK:    return "ODB_EULOCK";
		case ODB_EJOBDESC:  return "ODB_EJOBDESC";
		case ODB_EVERSION:  return "ODB_EVERSION";
		case ODB_EPROTO:    return "ODB_EPROTO";
		case ODB_EBADE:     return "ODB_EBADE";
		case ODB_EPIPE:     return "ODB_EPIPE";
		case ODB_EMISSED:   return "ODB_EMISSED";
		case ODB_EDELETED:  return "ODB_EDELETED";
		case ODB_EBUFFSIZE: return "ODB_EBUFFSIZE";
		default: return "UNDOCUMENTEDERROR";
	}
}