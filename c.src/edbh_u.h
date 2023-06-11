#ifndef EDBH_U_
#define EDBH_U_

#include "include/oidadb.h"
#include "edbh.h"
#include "edbs.h"

// note: this structure is initialized as 0 in odb_handle
typedef struct odbh {
	pid_t hostpid;
	edbs_handle_t *shm;

	void *user_cookie;


	// todo: need to invalidate these
	// todo: make sure these are initialized as 0
	struct odb_entstat *indexv;
	uint32_t indexc;
	struct odb_structstat *stkv;
	uint32_t stkc;
} odbh;



// private methods not found in oidadb.h
// We don't want the user downloading the index/structures everytime so we
// handle that privately and handle our own caching mechanism.
//
// In both cases, the o_ pointers will point them to arrays that are the
// return "length" long.
//
// CALLER IS RESPONSIBLE FOR CALLING free(o_entstat) / free(o_stkstat)
//
//
// later: these functions are temprory. We want to cache the results of what
//  these functiosn return
struct odbh_jobret odbh_jent_download(odbh *handle
		, struct odb_entstat **o_entstat);
struct odbh_jobret odbh_jstk_download(odbh *handle
		, struct odb_structstat **o_stkstat);

#endif