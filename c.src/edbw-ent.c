#include "edbw.h"
#include "edbw-util.h"

edb_err edbw_u_entjob(edb_worker_t *self) {

	// easy pointers
	edbs_jobhandler *job = &self->curjob;
	int jobdesc = job->job->jobdesc;
	edb_err err = 0;
	edba_handle_t *handle = &self->edbahandle;

	// get params
	edb_entry_t entryparams;
	edb_eid eid;
	switch (jobdesc & 0xFF00) {
		case EDB_CCREATE:
			edbs_jobread(job, &entryparams, sizeof(entryparams));
			err = edba_entryopenc(handle, &eid, EDBA_FCREATE);
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
		case EDB_CDEL:
			edbs_jobread(job, &eid, sizeof(eid));
			err = edba_entrydelete(handle, eid);
			edbs_jobwrite(job, &err, sizeof(err));
			return err;
		default:
			return EDB_EJOBDESC;
	}
	return err;
}