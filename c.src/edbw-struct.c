#include "edbw.h"


edb_err edbw_u_structjob(edb_worker_t *self) {

	// easy pointers
	edbs_jobhandler *job = &self->curjob;
	int jobdesc = job->job->jobdesc;
	edb_err err = 0;
	edba_handle_t *handle = &self->edbahandle;

	edb_struct_t *s;

	// check for some common errors regarding the edb_jobclass
	int ret;
	// all of these job classes need an id parameter
	ret = edbs_jobread(job, &oid, sizeof(oid));
	if(ret == -1) {
		err = EDB_ECRIT;
		edbs_jobwrite(job, &err, sizeof(err));
		return err;
	} else if(ret == -2) {
		err = EDB_EHANDLE;
		edbs_jobwrite(job, &err, sizeof(err));
		return err;
	}

}
