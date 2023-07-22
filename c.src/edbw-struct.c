#include <malloc.h>
#include "edbw.h"
#include "edbw_u.h"


static odb_err stkcreate(edb_worker_t *self) {
	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	// read odb_structstat
	struct odb_structstat stkstat;
	err = edbs_jobread(job, &stkstat, sizeof(stkstat));
	if(err) {
		return err;
	}

	// we're going to hold off on reading the confv, we need to make sure we can
	// allocate enough sapce to hold it first...

	// try to create a new structure
	odb_sid o_sid;
	odb_spec_struct_struct stk;
	stk.confc = stkstat.confc;
	stk.data_ptrc = stkstat.dynmc;
	stk.fixedc = stkstat.objc + sizeof(odb_spec_object_flags) + (stkstat.dynmc*sizeof(odb_dyptr)); // todo: this logic should be inside of edba_structopenc.
	stk.flags       = 0;
	err = edba_structopenc(handle, &o_sid, stk);
	if(err) {
		dieerror(job, err);
		return ODB_EUSER;
	}
	// **defer: edba_structclose

	// set the confv, if one was provided.
	void *confv = edba_structconfv_set(handle);
	edbs_jobread(job, confv, stk.confc);

	// close the structure: we're all done.
	edba_structclose(handle);

	// write out error+o_sid
	err = 0;
	edbs_jobwritev(job
			, &err, sizeof(err)
			, &o_sid, sizeof(o_sid)
			, 0);
	return 0;
}

static odb_err stkdelete(edb_worker_t *self) {
	// easy pointers
	edbs_job_t job = self->curjob;
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;
	odb_sid sid;

	// read sid
	err = edbs_jobread(job, &sid, sizeof(sid));
	if(err) {
		return err;
	}

	// open the strucutre
	err = edba_structopen(handle, sid);
	if (err) {
		dieerror(job,err);
		return ODB_EUSER;
	}

	// do the delete
	err = edba_structdelete(handle);
	if(err) {
		dieerror(job,err);
		return ODB_EUSER;
	}

	// delete successful
	err = 0;
	edbs_jobwrite(job, &err, sizeof(err));

	return 0;
}

static odb_err stkdownload(edb_worker_t *self) {
	// easy pointers
	edbs_job_t job = self->curjob;
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;
	odb_eid eid;

	// read dummy var
	uint32_t dummy;
	err = edbs_jobread(job, &dummy, sizeof(dummy));
	if(err) {
		return err;
	}

	// get structure data
	uint32_t count;
	struct odb_structstat *o_stks;
	err = edba_stks_get(handle, &count, 0);
	if(err) {
		dieerror(job,err);
		return ODB_EUSER;
	}
	o_stks = malloc(sizeof(struct odb_structstat) * count);
	err = edba_stks_get(handle, &count, o_stks);
	if(err) {
		dieerror(job,err);
		return ODB_EUSER;
	}

	// err+len
	err = 0;
	edbs_jobwrite(job, &err, sizeof(err));
	edbs_jobwrite(job, &count, sizeof(count));

	// the array
	edbs_jobwrite(job, o_stks, (int)sizeof(struct odb_structstat) * (int)count);

	// cleanup
	free(o_stks);

	return 0;
}

odb_err edbw_u_structjob(edb_worker_t *self) {

	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	switch (jobdesc) {
		case ODB_JSTKCREATE:   return stkcreate(self);
		case ODB_JSTKDELETE:   return stkdelete(self);
		case ODB_JSTKDOWNLOAD: return stkdownload(self);
		default:               return ODB_EJOBDESC;
	}

}
