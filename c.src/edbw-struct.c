#include "edbw.h"


odb_err edbw_u_structjob(edb_worker_t *self) {

	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	// working variables
	odb_spec_struct_struct s;
	odb_sid  structid;

	// per-description
	switch(jobdesc) {
		case ODB_JSTRCTCREATE:
			edbs_jobread(job, &s, sizeof(s));
			err = edba_structopenc(handle, &structid, s);
			if(err) {
				edbs_jobwrite(job, &err, sizeof(err));
				return err;
			}
			void *conf;
			edba_structconfset(handle, &conf);
			edbs_jobread(job, conf, s.confc);
			edba_structclose(handle);
			edbs_jobwrite(job, &structid, sizeof(structid));
			return 0;
		case ODB_JSTRCTDELETE:
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
			return ODB_EJOBDESC;
	}

}
