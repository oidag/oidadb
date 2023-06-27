#include "edbw.h"
#include "edbw_u.h"

static odb_err entcreate(edb_worker_t *self) {
	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	// read odbstruct
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
	odb_spec_index_entry ent = {0};
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
/*
	// get params
	odb_spec_index_entry entryparams;
	odb_eid eid;
	switch (jobdesc) {
		case ODB_JENTCREATE:
			edbs_jobread(job, &entryparams, sizeof(entryparams));
			err = edba_entryopenc(handle, &eid, EDBA_FCREATE | EDBA_FWRITE);
			if(err) {
				edbs_jobwrite(job, &err, sizeof(err));
				return err;
			}
			err = edba_entryset(handle, entryparams);
			if(err) {
				edba_entryclose(handle);
				edbs_jobwrite(job, &err, sizeof(err));
				return err;
			}
			edbs_jobwrite(job, &err, sizeof(err));
			edbs_jobwrite(job, &eid, sizeof(eid));
			edba_entryclose(handle);
			return 0;
		case ODB_JENTDELETE:
			edbs_jobread(job, &eid, sizeof(eid));
			err = edba_entrydelete(handle, eid);
			edbs_jobwrite(job, &err, sizeof(err));
			return err;
		default:
			return ODB_EJOBDESC;
	}
	return err;*/
}