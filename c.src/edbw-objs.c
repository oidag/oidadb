#define _GNU_SOURCE

#include "include/oidadb.h"
#include "edba.h"
#include "edbw.h"
#include "edbw_u.h"
#include "edbs-jobs.h"
#include "edbd.h" // todo remove this
#include "odb-structures.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

// returns 0 if not match
static int svid_good(uint32_t svid, odb_sid host_sid, uint16_t host_version) {
	if((odb_sid)(svid & 0xFFFF) != host_sid) return 0;
	if((uint16_t)(svid >> 0x10) != host_version) return 0;
	return 1;
}

// wrapper function for sending die-errors. As with a die error, you send it
// and ignore everything else they had sent.
void static dieerror(edbs_job_t job, odb_err err) {
	log_debugf("sending die-error: %s", odb_errstr(err));
	edbs_jobflush(job);  // todo: implement edbs_jobflush
	edbs_jobwrite(job, &err, sizeof(err));
}

// helper function used by both jread and jwrite sense they have so much in
// common.
odb_err static _jreadwrite(edb_worker_t *self, int iswrite) {
	odb_err err;
	edba_handle_t *handle = self->edbahandle;
	edbs_job_t job = self->curjob;
	odb_oid oid;
	uint32_t svid;

	// first read the oid+svid
	if((err = edbs_jobreadv(job
			, &oid, sizeof(oid)
			, &svid, sizeof(svid)
			,0))) {
		return err;
	}

	// get read access for that object
	if(iswrite) {
		err = edba_objectopen(handle, oid, EDBA_FREAD | EDBA_FWRITE);
	} else {
		err = edba_objectopen(handle, oid, EDBA_FREAD);
	}
	// consolidating errors for spec/edbs-jobs.org
	switch (err) {
		case 0: break;
		case ODB_ENOENT:
		case ODB_EEOF:
			err = ODB_ENOENT;
			//fallthrough
		default:
			// die-error
			dieerror(job, err);
			return ODB_EUSER;
	}

	// **defer: edb_objectclose(handle);

	// atp: we have the object opened

	// is it the same structure version?
	// Note we have to do this AFTER we run edba_objectopen so that we make
	// sure that this structure is then locked until edba_objectclosed
	odb_sid sid = edba_objectstructid(handle);
	const odb_spec_struct_struct *stk = edba_objectstruct(handle);
	if(!svid_good(svid, sid, stk->version)) {
		dieerror(job, ODB_ECONFLICT);
		edba_objectclose(handle);
		return ODB_EUSER;
	}

	// is it deleted?
	if(edba_objectdeleted(handle)) {
		dieerror(job, ODB_ENOENT);
		edba_objectclose(handle);
		return ODB_EUSER;
	}

	// is it locked?
	odb_usrlk usrlocks = edba_objectlocks_get(handle);
	err = 0;
	if(iswrite) {
		// we're trying to write so make sure EDB_FUSRLWR isn't present
		if (usrlocks & EDB_FUSRLWR) {
			err = ODB_EUSER;
		}
	} else {
		// we're trying to read so make sure EDB_FUSRLRD isn't present
		if (usrlocks & EDB_FUSRLRD) {
			err = ODB_EUSER;
		}
	}
	if(err) {
		dieerror(job, err);
		edba_objectclose(handle);
		return ODB_EUSER;
	}


	if (iswrite) {
		// read in the entire object.
		edbs_jobread(job, edba_objectfixed(handle), stk->fixedc);
		// we actually continue past this because we want to write a no
		// die-error
	}

	// no die-error
	err = 0;
	edbs_jobwrite(job, &err, sizeof(err));

	if(!iswrite) {
		// send the object to the handle
		edbs_jobwrite(job, edba_objectfixed_get(handle), stk->fixedc);
	}
	return 0;
}

odb_err static jwrite(edb_worker_t  *self) {
	return _jreadwrite(self, 1);
}

// returns errors edbs_jobread
odb_err static jread(edb_worker_t *self) {
	return _jreadwrite(self, 0);
}

// job data is assumed to be EDB_OBJ
odb_err edbw_u_objjob(edb_worker_t *self) {

	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);
	odb_err err = 0;
	edba_handle_t *handle = self->edbahandle;

	// some easy variables we'll be needing
	const odb_spec_struct_struct *strt;
	odb_usrlk usrlocks;
	void *data;
	odb_oid oid;
	int ret;

	switch (jobdesc) {
		case ODB_JWRITE: return jwrite(self);
		case ODB_JREAD: return jread(self);
		default:

	}

	// if err is non-0 after this then it will close
	// **defer: edba_objectclose
	implementme(); // todo: the comment block below has been commented out
	// sense the change of the job types.
	/*switch (jobdesc & 0xFF00) {
		case ODB_CDEL:
		case ODB_CUSRLKW:
		case ODB_CWRITE:
			// all require write access
			err = edba_objectopen(handle, oid, EDBA_FWRITE);
			break;

		case ODB_CCREATE:
			if((oid & EDB_OID_AUTOID) != EDB_OID_AUTOID) {
				// find an ID to use.
				err = edba_objectopenc(handle, &oid, EDBA_FWRITE | EDBA_FCREATE);
			} else {
				// use the existing ID
				err = edba_objectopen(handle, oid, EDBA_FWRITE);
			}
			break;

		case ODB_CUSRLKR:
		case ODB_CREAD:
			// read only
			err = edba_objectopen(handle, oid, 0);
			break;

		default:
			return ODB_EJOBDESC;
	}
	if(err) {
		edbs_jobwrite(self->curjob, &err, sizeof(err));
		return err;
	}

	// do the routing
	switch (jobdesc) {
		case EDB_OBJ | ODB_CCREATE:
			edbw_logverbose(self, "copy object: 0x%016lX", oid);

			// make sure this oid isn't already deleted
			if(!edba_objectdeleted(handle)) {
				err = ODB_EEXIST;
				edbs_jobwrite(self->curjob, &err, sizeof(err));
				break;
			}

			// lock check
			usrlocks = edba_objectlocks_get(handle);
			if(usrlocks & EDB_FUSRLCREAT) {
				err = ODB_EULOCK;
				edbs_jobwrite(self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(self->curjob, &err, sizeof(err));

			// write to the created object
			strt = edba_objectstruct(handle);
			data  = edba_objectfixed(handle);
			edbs_jobread(self->curjob, data, strt->fixedc);

			// mark this object as undeleted
			edba_objectundelete(handle);

			// return the oid of the created object
			edbs_jobwrite(self->curjob, &oid, sizeof(oid));
			break;

		case EDB_OBJ | ODB_CREAD:
			edbw_logverbose(self, "copy object: 0x%016lX", oid);

			// is it deleted?
			if(edba_objectdeleted(handle)) {
				err = ODB_ENOENT;
				edbs_jobwrite(self->curjob, &err, sizeof(err));
				break;
			}

			// lock check
			usrlocks = edba_objectlocks_get(handle);
			if(usrlocks & EDB_FUSRLRD) {
				err = ODB_EULOCK;
				edbs_jobwrite(self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(self->curjob, &err, sizeof(err));

			// write out the object.
			strt = edba_objectstruct(handle);
			data  = edba_objectfixed(handle);
			edbs_jobwrite(self->curjob, data, strt->fixedc);
			break;

		case EDB_OBJ | ODB_CWRITE:
			edbw_logverbose(self, "edit object 0x%016lX", oid);

			// is it deleted?
			if(edba_objectdeleted(handle)) {
				err = ODB_ENOENT;
				edbs_jobwrite(self->curjob, &err, sizeof(err));
				break;
			}

			// lock check
			usrlocks = edba_objectlocks_get(handle);
			if(usrlocks & EDB_FUSRLWR) {
				err = ODB_EULOCK;
				edbs_jobwrite(self->curjob, &err, sizeof(err));
				break;
			}

			// err 0
			err = 0;
			edbs_jobwrite(self->curjob, &err, sizeof(err));

			// update the record
			strt = edba_objectstruct(handle);
			data  = edba_objectfixed(handle);
			edbs_jobread(self->curjob, data, strt->fixedc);

			break;
		case EDB_OBJ | ODB_CDEL:
			// object-deletion
			edbw_logverbose(self, "delete object 0x%016lX", oid);

			// lock check
			usrlocks = edba_objectlocks_get(handle);
			if(usrlocks & EDB_FUSRLWR) {
				err = ODB_EULOCK;
				edbs_jobwrite(self->curjob, &err, sizeof(err));
				break;
			}

			// (note we don't mark as no-error until after the delete)

			// perform the delete
			err = edba_objectdelete(handle);
			edbs_jobwrite(self->curjob, &err, sizeof(err));
			break;

		case EDB_OBJ | ODB_CUSRLKR:
			edbw_logverbose(self, "cuserlock read object 0x%016lX", oid);

			// err 0
			err = 0;
			edbs_jobwrite(self->curjob, &err, sizeof(err));

			// read-out locks
			usrlocks = edba_objectlocks_get(handle);
			edbs_jobwrite(self->curjob, &usrlocks, sizeof(odb_usrlk));
			break;

		case EDB_OBJ | ODB_CUSRLKW:
			edbw_logverbose(self, "cuserlock write object 0x%016lX", oid);

			// err 0
			err = 0;
			edbs_jobwrite(self->curjob, &err, sizeof(err));

			// update locks
			edbs_jobread(self->curjob, &usrlocks, sizeof(odb_usrlk));
			edba_objectlocks_set(handle, usrlocks & _EDB_FUSRLALL);
			break;
		default:break;
	}*/
	edba_objectclose(handle);
	return 0;
}