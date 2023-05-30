#define _GNU_SOURCE

#include "edbh_u.h"
#include "include/oidadb.h"
#include "edbh_u.h"
#include "edbh.h"
#include "errors.h"
#include "edbs.h"
#include "edbs-jobs.h"

struct odbh_jobret odbh_jent_create(odbh *handle
		, struct odb_entstat entstat) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0
	   || entstat.type != ODB_ELMOBJ) {
		ret.err=ODB_EINVAL;
		return ret;
	}

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JENTCREATE, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the eid+objectdata
	if((ret.err = edbs_jobwrite(job
			, &entstat, sizeof(struct odb_entstat)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err+sid
	odb_err dieerr;
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)
			, &ret.eid, sizeof(ret.eid)))) {
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

struct odbh_jobret odbh_jent_free(odbh *handle
		, odb_eid eid) {
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
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JENTDELETE, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the eid
	if((ret.err = edbs_jobwrite(job
			, &eid, sizeof(eid)))) {
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