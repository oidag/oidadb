#include "edbw.h"
#include "edbw_u.h"

odb_err edbw_u_entjob(edb_worker_t *self) {

	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	// get params
	odb_spec_index_entry entryparams;
	odb_eid eid;
	switch (jobdesc & 0xFF00) {
		case ODB_CCREATE:
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
		case ODB_CDEL:
			edbs_jobread(job, &eid, sizeof(eid));
			err = edba_entrydelete(handle, eid);
			edbs_jobwrite(job, &err, sizeof(err));
			return err;
		default:
			return ODB_EJOBDESC;
	}
	return err;
}