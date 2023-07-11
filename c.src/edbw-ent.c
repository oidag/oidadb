#include <malloc.h>
#include "edbw.h"
#include "edbw_u.h"

static odb_err entcreate(edb_worker_t *self) {
	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	// read odb_entstat
	struct odb_entstat entstat;
	err = edbs_jobread(job, &entstat, sizeof(entstat));
	if(err) {
		return err;
	}

	// create the entry
	odb_eid eid;
	err = edba_entryopenc(handle, &eid, EDBA_FWRITE | EDBA_FCREATE);
	if(err) {
		dieerror(job, err);
		return ODB_EUSER;
	}
	// **defer: edba_entryclose

	// translate the struct odb_entstat into a odb_spec_index_entry
	odb_spec_index_entry ent;
	ent.structureid = entstat.structureid;
	ent.type        = entstat.type;
	ent.memory      = entstat.memorysettings;

	// set the entry
	err = edba_entryset(handle, ent);
	edba_entryclose(handle);
	if(err) {
		// normalize the error for edbs-jobs.org
		switch(err) {
			case ODB_EEOF:
				err = ODB_ENOENT;
				break;
			case ODB_ENOMEM:
			case ODB_ENOSPACE:
				err = log_critf("unhandled error for entry creation");
				break;
			default:break;
		}
		dieerror(job, err);
		return ODB_EUSER;
	}

	// write the err+eid.
	err = 0;
	edbs_jobwritev(job
				   , &err, sizeof(err)
				   , &eid, sizeof(eid)
				   , 0);
	return 0;
}

static odb_err entdelete(edb_worker_t *self) {
	// easy pointers
	edbs_job_t job = self->curjob;
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;
	odb_eid eid;

	// read eid
	err = edbs_jobread(job, &eid, sizeof(eid));
	if(err) {
		return err;
	}

	// do the delete
	err = edba_entrydelete(handle, eid);
	if(err) {
		dieerror(job,err);
		return ODB_EUSER;
	}

	// delete successful
	err = 0;
	edbs_jobwrite(job, &err, sizeof(err));

	return 0;
}

static odb_err entdownload(edb_worker_t *self) {
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

	// get entityt data
	uint32_t count;
	struct odb_entstat *o_ents;
	err = edba_entity_get(handle, &count, &o_ents);
	if(err) {
		dieerror(job,err);
		return ODB_EUSER;
	}

	// err+len
	err = 0;
	edbs_jobwrite(job, &err, sizeof(err));
	edbs_jobwrite(job, &count, sizeof(count));

	// the array
	edbs_jobwrite(job, o_ents, (int)sizeof(struct odb_entstat) * (int)count);

	// cleanup
	free(o_ents);

	return 0;
}

odb_err edbw_u_entjob(edb_worker_t *self) {

	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	switch (jobdesc) {
		case ODB_JENTCREATE:   return entcreate(self);
		case ODB_JENTDELETE:   return entdelete(self);
		case ODB_JENTDOWNLOAD: return entdownload(self);
		default:               return ODB_EJOBDESC;
	}
}