#ifndef _edbHOST_H_
#define _edbHOST_H_

#include <sys/types.h>

#include "include/ellemdb.h"
#include "errors.h"

typedef struct edb_host_st edb_host_t;

typedef struct edb_job_st {

} edb_job_t;

// stored the pid of the host for a given database file.
// does not validate the file itself.
//
// Errors:
//    EDB_ENOHOST - no host for file
//    EDB_EERRNO - error with open(2).
edb_err edb_host_getpid(const char *path, pid_t *outpid);

#endif