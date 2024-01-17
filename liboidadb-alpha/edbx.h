#ifndef _edbHOST_H_
#define _edbHOST_H_

#include "edbs.h"
#include <oidadb/oidadb.h>
#include "errors.h"

#include <sys/types.h>
#include <unistd.h>

enum hoststate {
	HOST_NONE = 0,
	HOST_OPENING_DESCRIPTOR,
	HOST_OPENING_XLLOCK,
	HOST_OPENING_FILE,
	HOST_OPENING_SHM,
	HOST_OPENING_PAGEBUFF,
	HOST_OPENING_ARTICULATOR,
	HOST_OPENING_WORKERS,
	HOST_OPEN,

	HOST_CLOSING,
};

typedef struct edb_host_st edb_host_t;



#endif