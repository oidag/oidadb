#ifndef EDBH_U_
#define EDBH_U_

#include "include/oidadb.h"
#include "edbh.h"
#include "edbs.h"

typedef struct odbh {
	pid_t hostpid;
	edbs_handle_t *shm;
} odbh;

#endif