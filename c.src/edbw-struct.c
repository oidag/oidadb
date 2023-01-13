#include "edbw.h"


edb_err edbw_u_structjob(edb_worker_t *self) {

	// easy pointers
	edbs_jobhandler *job = &self->curjob;
	int jobdesc = job->job->jobdesc;
	edb_err err = 0;
	edba_handle_t *handle = &self->edbahandle;

	// working variables
	edb_struct_t s;
	edb_sid  structid;

	// per-description
	switch(jobdesc & 0xFF00) {
		case EDB_CCREATE:
			edbs_jobread(job, &s, sizeof(s));
			err = edba_structopenc(handle, &structid, s);
			if(err) {
				edbs_jobwrite(job, &err, sizeof(err));
				return err;
			}
			void *conf = edba_structconf(handle);
			edbs_jobread(job, conf, s.confc);
			edba_structclose(handle);
			edbs_jobwrite(job, &structid, sizeof(structid));
			return 0;
		case EDB_CDEL:
			edbs_jobread(job, &structid, sizeof(structid));
			err = edba_structopen(handle, structid);
			if(err) {
				edbs_jobwrite(job, &err, sizeof(err));
				return err;
			}
			err = edba_structdelete(handle);
			edbs_jobwrite(job, &err, sizeof(err));
			return err;
		default:
			return EDB_EJOBDESC;
	}

}
