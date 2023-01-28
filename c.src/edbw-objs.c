#include <stddef.h>
#define _LARGEFILE64_SOURCE
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>


#include "include/oidadb.h"
#include "edba.h"
#include "edbw.h"
#include "edbw_u.h"
#include "edbs-jobs.h"
#include "edbd.h"
#include "edbp-types.h"

// job data is assumed to be EDB_OBJ
edb_err edbw_u_objjob(edb_worker_t *self) {

	// easy pointers
	edbs_jobhandler *job = &self->curjob;
	int jobdesc = job->job->jobdesc;
	edb_err err = 0;
	edba_handle_t *handle = &self->edbahandle;

	// check for some common errors regarding the edb_jobclass
	edb_oid oid;
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

	// some easy variables we'll be needing
	const edb_struct_t *strt;
	odb_usrlk usrlocks;
	void *data;

	// if err is non-0 after this then it will close
	// **defer: edba_objectclose
	switch (jobdesc & 0xFF00) {
		case EDB_CDEL:
		case EDB_CUSRLKW:
		case EDB_CWRITE:
			// all require write access
			err = edba_objectopen(handle, oid, EDBA_FWRITE);
			break;

		case EDB_CCREATE:
			if((oid & EDB_OID_AUTOID) != EDB_OID_AUTOID) {
				// find an ID to use.
				err = edba_objectopenc(handle, &oid, EDBA_FWRITE | EDBA_FCREATE);
			} else {
				// use the existing ID
				err = edba_objectopen(handle, oid, EDBA_FWRITE);
			}
			break;

		case EDB_CUSRLKR:
		case EDB_CCOPY:
			// read only
			err = edba_objectopen(handle, oid, 0);
			break;

		default:
			return EDB_EJOBDESC;
	}
	if(err) {
		edbs_jobwrite(&self->curjob, &err, sizeof(err));
		return err;
	}

	// do the routing
	switch (jobdesc) {
		case EDB_OBJ | EDB_CCREATE:
			edbw_logverbose(self, "copy object: 0x%016lX", oid);

			// make sure this oid isn't already deleted
			if(!edba_objectdeleted(handle)) {
				err = EDB_EEXIST;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// lock check
			usrlocks = edba_objectlocks_get(handle);
			if(usrlocks & EDB_FUSRLCREAT) {
				err = EDB_EULOCK;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// write to the created object
			strt = edba_objectstruct(handle);
			data  = edba_objectfixed(handle);
			edbs_jobread(&self->curjob, data, strt->fixedc);

			// mark this object as undeleted
			edba_objectundelete(handle);

			// return the oid of the created object
			edbs_jobwrite(&self->curjob, &oid, sizeof(oid));
			break;

		case EDB_OBJ | EDB_CCOPY:
			edbw_logverbose(self, "copy object: 0x%016lX", oid);

			// is it deleted?
			if(edba_objectdeleted(handle)) {
				err = EDB_ENOENT;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// lock check
			usrlocks = edba_objectlocks_get(handle);
			if(usrlocks & EDB_FUSRLRD) {
				err = EDB_EULOCK;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// write out the object.
			strt = edba_objectstruct(handle);
			data  = edba_objectfixed(handle);
			edbs_jobwrite(&self->curjob, data, strt->fixedc);
			break;

		case EDB_OBJ | EDB_CWRITE:
			edbw_logverbose(self, "edit object 0x%016lX", oid);

			// is it deleted?
			if(edba_objectdeleted(handle)) {
				err = EDB_ENOENT;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// lock check
			usrlocks = edba_objectlocks_get(handle);
			if(usrlocks & EDB_FUSRLWR) {
				err = EDB_EULOCK;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// update the record
			strt = edba_objectstruct(handle);
			data  = edba_objectfixed(handle);
			edbs_jobread(&self->curjob, data, strt->fixedc);

			break;
		case EDB_OBJ | EDB_CDEL:
			// object-deletion
			edbw_logverbose(self, "delete object 0x%016lX", oid);

			// lock check
			usrlocks = edba_objectlocks_get(handle);
			if(usrlocks & EDB_FUSRLWR) {
				err = EDB_EULOCK;
				edbs_jobwrite(&self->curjob, &err, sizeof(err));
				break;
			}

			// (note we don't mark as no-error until after the delete)

			// perform the delete
			err = edba_objectdelete(handle);
			edbs_jobwrite(&self->curjob, &err, sizeof(err));
			break;

		case EDB_OBJ | EDB_CUSRLKR:
			edbw_logverbose(self, "cuserlock read object 0x%016lX", oid);

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// read-out locks
			usrlocks = edba_objectlocks_get(handle);
			edbs_jobwrite(&self->curjob, &usrlocks, sizeof(odb_usrlk));
			break;

		case EDB_OBJ | EDB_CUSRLKW:
			edbw_logverbose(self, "cuserlock write object 0x%016lX", oid);

			// err 0
			err = 0;
			edbs_jobwrite(&self->curjob, &err, sizeof(err));

			// update locks
			edbs_jobread(&self->curjob, &usrlocks, sizeof(odb_usrlk));
			edba_objectlocks_set(handle, usrlocks & _EDB_FUSRLALL);
			break;
		default:break;
	}
	edba_objectclose(handle);
	return 0;
}