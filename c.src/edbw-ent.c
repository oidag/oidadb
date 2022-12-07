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
	switch (jobdesc & 0xFF00) {
		case EDB_CCREATE:
			edbs_jobread(job, &entryparams, sizeof(entryparams));
		case EDB_CDEL:
		default:
			return EDB_EJOBDESC;
	}
	return 0;
}