#ifndef EDB_EDBW_UTIL_H
#define EDB_EDBW_UTIL_H

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
//   - EDB_EJOBDESC: a special log_errorf notice is made
edb_err edbw_u_objjob(edb_worker_t *self);
edb_err edbw_u_entjob(edb_worker_t *self);

#endif //EDB_EDBW_UTIL_H
