#define _GNU_SOURCE

#include <malloc.h>
#include <errno.h>
#include <strings.h>
#include <stdatomic.h>
#include "edbh_u.h"
#include "include/oidadb.h"
#include "edbh_u.h"
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

// helper method to all odb_jobj definitoins
odb_err static eid2struct(odbh *handle, odb_eid eid, struct odb_structstat
*o_stat) {
	struct odb_entstat entstat;
	odb_err err;
	if((err = odbh_index(handle, eid, &entstat))) {
		if(err != ODB_ECRIT) {
			// we will conclude any errors outside of a critical error can be
			// assumed to be used as this function's definition of ODB_ENOENT.
			err = ODB_ENOENT;
		}
		return err;
	}
	if((err = odbh_structs(handle, entstat.structureid, o_stat))) {
		// yeah there should be no reason this returns an error.
		log_critf("unhandled error %d", err);
		err = ODB_ECRIT;
		return err;
	}
	return 0;
}

struct odbh_jobret odbh_jobj_alloc(odbh *handle
		, odb_eid eid
		, const void *usrobj) {

	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {ret.err=ODB_EINVAL; return ret;};
	if(usrobj == 0) {ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// get structure
	struct odb_structstat structstat;
	if((ret.err = eid2struct(handle, eid, &structstat))) {
		return ret;
	}

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JALLOC, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the eid+objectdata
	if((ret.err = edbs_jobwrite(job
			, &eid, sizeof(eid)
			, usrobj, structstat.fixedc))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err+oid
	odb_err dieerr;
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)
			, &ret.oid, sizeof(ret.oid)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// successful execution
	return ret;
}


struct odbh_jobret odbh_jobj_free(odbh *handle
		, odb_oid oid) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JFREE, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the oid
	if((ret.err = edbs_jobwrite(job
			, &oid, sizeof(oid)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err
	odb_err dieerr;
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// successful execution
	return ret;
}

struct odbh_jobret odbh_jobj_write(odbh *handle
		, odb_oid oid
		, const void *usrobj) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {ret.err=ODB_EINVAL; return ret;};
	if(usrobj == 0) {ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// get structure
	struct odb_structstat structstat;
	if((ret.err = eid2struct(handle, odb_oid_get_eid(oid), &structstat))) {
		return ret;
	}

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JWRITE, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the oid+objectdata
	if((ret.err = edbs_jobwrite(job
			, &oid, sizeof(oid)
			, usrobj, structstat.fixedc))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err
	odb_err dieerr;
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// successful execution
	return ret;
}

struct odbh_jobret odbh_jobj_read(odbh *handle
		, odb_oid oid
		, void *usrobj) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {ret.err=ODB_EINVAL; return ret;};
	if(usrobj == 0) {ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// get structure
	struct odb_structstat structstat;
	if((ret.err = eid2struct(handle, odb_oid_get_eid(oid), &structstat))) {
		return ret;
	}

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JREAD, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the oid
	if((ret.err = edbs_jobwrite(job
			, &oid, sizeof(oid)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err+usrobj
	odb_err dieerr;
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)
			, usrobj, structstat.fixedc))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// successful execution
	return ret;
}

