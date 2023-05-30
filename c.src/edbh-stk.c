#define _GNU_SOURCE

#include "edbh_u.h"
#include "include/oidadb.h"
#include "edbh_u.h"
#include "edbh.h"
#include "errors.h"
#include "edbs.h"
#include "edbs-jobs.h"

struct odbh_jobret odbh_jstk_create(odbh *handle
		, struct odb_structstat structstat) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0
	   || structstat.fixedc < 4
	   || structstat.fixedc > INT16_MAX
	   || structstat.dynmc > INT16_MAX
	   || structstat.confc > INT16_MAX
	   ) {
		ret.err=ODB_EINVAL;
		return ret;
	}

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JSTKCREATE, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// so even though the protocol is designed to ignore confv, something
	// feels... off... about sending memory addresses over the network. My
	// intuition of security is bothering me. So I'll just copy the structure
	// and make sure we send out a null pointer confv.
	struct odb_structstat statcpy = structstat;
	statcpy.confv = 0;

	// write the eid+objectdata
	if((ret.err = edbs_jobwrite(job
			, &statcpy, sizeof(struct odb_structstat)
			, structstat.confv, structstat.confc))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err+sid
	odb_err dieerr;
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)
			, &ret.sid, sizeof(ret.sid)))) {
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

struct odbh_jobret odbh_jstk_free(odbh *handle
		, odb_sid sid) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {
		ret.err=ODB_EINVAL;
		return ret;
	}

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JSTKDELETE, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the sid
	if((ret.err = edbs_jobwrite(job
			, &sid, sizeof(sid)))) {
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


