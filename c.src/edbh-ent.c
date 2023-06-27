#define _GNU_SOURCE

#include <malloc.h>
#include "edbh_u.h"
#include "include/oidadb.h"
#include "edbh_u.h"
#include "edbh.h"
#include "errors.h"
#include "edbs.h"
#include "edbs-jobs.h"


// helper function to odbh_jent_download and odbh_jstk_download
// because both of their protocols are the same.
struct odbh_jobret odbh_jent_download(odbh *handle
		, struct odb_entstat **o_entstat) {

	struct odbh_jobret ret;
	edbs_job_t job;
	odb_jobtype_t jobt = ODB_JENTDOWNLOAD;
	size_t size = sizeof(struct odb_entstat);

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, jobt, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the dummy proto
	uint32_t dummy = 1;
	if((ret.err = edbs_jobwritev(job
			, dummy, sizeof(dummy)
			, 0))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err+len
	odb_err dieerr;
	if((ret.err = edbs_jobreadv(job
			, &dieerr, sizeof(dieerr)
			, &ret.length, sizeof(ret.length)
			, 0))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// allocate space for the index
	void *output = malloc(size
			* ret.length);
	if(output == 0) {
		log_critf("failed to allocate during download");
		ret.err = ODB_ECRIT;
		return ret;
	}

	// do the download
	for(int i = 0; i < ret.length; i++) {
		if((ret.err = edbs_jobread(job
				, output + size * i
				, size)
		)) {
			free(output);
			ret.err = edbs_joberr_trunc(ret.err);
			return ret;
		}
	}
	*o_entstat = output;

	// successful execution
	return ret;
}

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

	// write the entstat
	if((ret.err = edbs_jobwrite(job
			, &entstat, sizeof(struct odb_entstat)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err+sid
	odb_err dieerr;
	if((ret.err = edbs_jobreadv(job
			, &dieerr, sizeof(dieerr)
			, &ret.eid, sizeof(ret.eid)
			, 0))) {
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