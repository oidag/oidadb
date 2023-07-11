#ifndef EDB_EDBW_U_H
#define EDB_EDBW_U_H

// the only thing that can include this file is other c files
// of the same namespace

// these are all helper functions to execjob found in edbw.c
// Going into these functions the following is assumed:
//   - self has a valid job selected
//   - the jobdescription falls in the function's namespace (EDB_OBJ, EDB_DYM, ect)
//   - once these function return, the jobclose will be called regardless of error.
//
// RETURNS:
// All errors returned by these functions are ignored with the following
// special exceptions:
//   - ODB_EJOBDESC: a special log_errorf notice is made
odb_err edbw_u_objjob(edb_worker_t *self);
odb_err edbw_u_entjob(edb_worker_t *self);
odb_err edbw_u_structjob(edb_worker_t *self);


// wrapper function for sending die-errors. As with a die error, you send it
// and ignore everything else they had sent.
void static dieerror(edbs_job_t job, odb_err err) {
	log_debugf("sending die-error: %s", odb_errstr(err));
	edbs_jobwrite(job, &err, sizeof(err));
}

#endif //EDB_EDBW_U_H
