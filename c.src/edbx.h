#ifndef _edbHOST_H_
#define _edbHOST_H_

#include <sys/types.h>
#include <unistd.h>

#include "edbs.h"
#include "include/ellemdb.h"
#include "errors.h"

enum hoststate {
	HOST_NONE = 0,
	HOST_CLOSED,
	HOST_CLOSING,
	HOST_OPEN,
	HOST_OPENING,
	HOST_FAILED,
};

typedef struct edb_host_st edb_host_t;

// stored the pid of the host for a given database file.
// does not validate the file itself.
//
// Errors:
//    EDB_ENOHOST - no host for file
//    EDB_EERRNO - error with open(2).
edb_err edb_host_getpid(const char *path, pid_t *outpid);



#endif