#ifndef _edbHOST_H_
#define _edbHOST_H_

#include "errors.h"


typedef struct edb_worker_st edb_worker_t;

// stored the pid of the host for a given database file.
// does not validate the file itself.
//
// Errors:
//    EDB_ENOHOST - no host for file
//    EDB_EERRNO - error with open(2).
edb_err edb_host_getpid(const char *path, pid_t *outpid);

#endif