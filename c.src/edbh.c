#define _GNU_SOURCE

#include <malloc.h>
#include <errno.h>
#include <strings.h>
#include <stdatomic.h>
#include "edbh_u.h"
#include "include/oidadb.h"
#include "edbh.h"
#include "errors.h"
#include "edbs.h"
#include "edbs-jobs.h"

// used to create warnings
// see https://gcc.gnu.org/onlinedocs/gcc/Thread-Local.html
static __thread int safety_threadwarn = 0;

odb_err odb_handle(const char *path, odbh **o_handle) {

	// invals
	if(!path || !o_handle) {
		return ODB_EINVAL;
	}

	// provide a warning if they're creating multiple handles on the same
	// thread. See function documentation as to why this is bad.
	if(safety_threadwarn) {
		log_warnf("attempt to create multiple odbh instances via odb_handle "
				  "on the same thread: each thread should call odb_handle "
				  "independently");
	}

	// easy vars
	odb_err err;
	odbh *ret = malloc(sizeof(odbh));
	if(!ret) {
		if(errno == ENOMEM) {
			return ODB_ENOMEM;
		}
		return ODB_EERRNO;
	}
	bzero(ret, sizeof(odbh));

	// get pid.
	if((err = edb_host_getpid(path, &ret->hostpid))) {
		goto defer_dealloc;
	}

	// connect to shm
	if((err = edbs_handle_init(&ret->shm, ret->hostpid))) {
		goto defer_dealloc;
	}

	*o_handle = ret;
	atomic_fetch_add(&safety_threadwarn, 1);
	return 0;

	// defers:
	defer_shm:
	edbs_handle_free(ret->shm);
	defer_dealloc:
	free(ret);
	return err;
}
void odb_handleclose(odbh *handle) {
	// todo: should handle close implictly call odb_jclose just incase?
	edbs_handle_free(handle->shm);
	free(handle);
	atomic_fetch_sub(&safety_threadwarn, 1);
}

odb_err odbh_job(odbh *handle, odb_jobtype_t jobclass) {

	// invals
	if (!handle) {
		return ODB_EINVAL;
	}

	// install the job
	odb_err err;
	edbs_job_t job;
	if ((err = edbs_jobinstall(handle->shm, jobclass, &job))) {
		return err;
	}
	// todo;
	implementme();
}

/*

odb_err edb_open(edbh *handle, edb_open_t params) {

	// check for easy ODB_EINVAL
	if(handle == 0 || params.path == 0)
		return ODB_EINVAL;

	// get the hostpid.
	odb_err hosterr = edb_host_getpid(params.path, &(handle->hostpid));
	if(hosterr) {
		return hosterr;
	}

	// at this point, the file exist and has a host attached to it.
	// said host's pid is stored in handle->hostpid

	// todo: figure out how the handle will call other things from the host...

}


odb_err edb_close(edbh *handle);*/