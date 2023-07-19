#define _GNU_SOURCE

#include "include/oidadb.h"
#include "edba.h"
#include "edbw.h"
#include "edbw_u.h"
#include "edbs-jobs.h"
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
	struct odb_structstat stkstat = edba_objectstructstat(handle);
	if(stkstat.svid != svid) {
		dieerror(job, ODB_ECONFLICT);
		edba_objectclose(handle);
		return ODB_EUSER;
	}

	// is it deleted?
	if(edba_objectdeleted(handle)) {
		dieerror(job, ODB_EDELETED);
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
		edbs_jobread(job, edba_objectfixed(handle), (int)(stkstat.fixedc - stkstat.start));
		// we actually continue past this because we want to write a no
		// die-error
	}

	// no die-error
	err = 0;
	edbs_jobwrite(job, &err, sizeof(err));

	if(!iswrite) {
		// send the object to the handle
		edbs_jobwrite(job, edba_objectfixed_get(handle), (int)(stkstat.fixedc - stkstat.start));
	}

	// done, close out.
	edba_objectclose(handle);
	return 0;
}

odb_err static jwrite(edb_worker_t  *self) {
	return _jreadwrite(self, 1);
}

// returns errors edbs_jobread
odb_err static jread(edb_worker_t *self) {
	return _jreadwrite(self, 0);
}

odb_err static jalloc(edb_worker_t *self) {
	odb_err err;
	edba_handle_t *handle = self->edbahandle;
	edbs_job_t job = self->curjob;
	odb_eid eid;
	odb_oid out_oid;
	uint32_t svid;

	// first read the eid+svid
	if((err = edbs_jobreadv(job
			, &eid, sizeof(eid)
			, &svid, sizeof(svid)
			,0))) {
		return err;
	}

	// create a new object
	out_oid = odb_oid_set_eid(0, eid);
	err = edba_objectopenc(handle, &out_oid, EDBA_FREAD | EDBA_FCREATE | EDBA_FWRITE);
	// error consolidation for edbs-jobs.org
	switch (err) {
		case 0: break;
		case ODB_EEOF:
			err = ODB_ENOENT;
			// fallthrouhg
		default:
			// die-error
			dieerror(job, err);
			return ODB_EUSER;
	}

	// is it the same structure version?
	odb_sid sid = edba_objectstructid(handle);
	struct odb_structstat stkstat = edba_objectstructstat(handle);
	if(svid != stkstat.svid) {
		dieerror(job, ODB_ECONFLICT);
		edba_objectclose(handle);
		return ODB_EUSER;
	}

	// read in the entire object.
	edbs_jobread(job, edba_objectfixed(handle), (int)stkstat.fixedc - (int)stkstat.start);

	// no die-error
	err = 0;
	edbs_jobwrite(job, &err, sizeof(err));

	// write the new object id
	edbs_jobwrite(job, &out_oid, sizeof(out_oid));

	// done.
	edba_objectclose(handle);
	return 0;
}

odb_err static jfree(edb_worker_t *self) {
	odb_err err;
	edba_handle_t *handle = self->edbahandle;
	edbs_job_t job = self->curjob;
	odb_eid eid;
	odb_oid oid;
	uint32_t svid;

	// first read the oid
	if((err = edbs_jobreadv(job
			, &oid, sizeof(oid)
			,0))) {
		return err;
	}

	err = edba_objectopen(handle, oid, EDBA_FWRITE);
	// error consolidation for edbs-jobs.org
	switch (err) {
		case 0: break;
		case ODB_EEOF:
			err = ODB_ENOENT;
			// fallthrouhg
		default:
			// die-error
			dieerror(job, err);
			return ODB_EUSER;
	}

	if(edba_objectdeleted(handle)) {
		edba_objectclose(handle);
		dieerror(job, ODB_EDELETED);
		return 0;
	}

	// do the delete
	err = edba_objectdelete(handle);
	dieerror(job, 0);
	edba_objectclose(handle);
	return 0;
}

// helper function used by jupdate and jselect sense they have so much in
// common.
odb_err static _jupdateselect(edb_worker_t *self, int jselect) {
	odb_err err;
	edba_handle_t *handle = self->edbahandle;
	edbs_job_t job = self->curjob;
	odb_eid eid;
	uint32_t svid;
	odb_pid page_start, page_cap;


	// read eid/page_start/page_cap/SVID
	err = edbs_jobreadv(job
				  , &eid, sizeof(eid)
				  , &page_start, sizeof(page_start)
				  , &page_cap, sizeof(page_cap)
				  , &svid, sizeof(svid)
				  ,0);
	if(err) {
		return err;
	}

	// open the first page.
	err = edba_pageopen(handle, eid, page_start, EDBA_FREAD);
	if (err) {
		dieerror(job, err);
		return ODB_EUSER;
	}

	// **defer: edba_pageclose(handle);

	// check the svid.
	odb_sid sid = edba_pagestructid(handle);
	const odb_spec_struct_struct *stk = edba_pagestruct(handle);
	if(!svid_good(svid, sid, stk->version)) {
		dieerror(job, ODB_ECONFLICT);
		edba_pageclose(handle);
		return ODB_EUSER;
	}

	// all errors have been checked. let us send a 0error
	err = 0;
	edbs_jobwrite(job, &err, sizeof(err));

	// now loop through each page and send over the entire body of each page.
	for(int i = 0; i < page_cap; i++) {

		// note: we start this forloop with the page already checked out, so
		// we check for errors at the bottom of this for loop when we check
		// out the next page.

		// get the objects on this page.
		uint32_t object_count = edba_pageobjectv_count(handle);

		// now send them the entire page body.
		// get the object body.
		// note: objectv can be const, used for both jselect and jupdate.
		void *objecv;
		unsigned int objectc = edba_pageobjectc(handle);
		if(jselect) {
			// get write-ready page
			objecv = edba_pageobjectv(handle);
		} else {
			// get read-only page.
			objecv = (void *)edba_pageobjectv_get(handle);
		}
		err = edbs_jobwritev(job
				, &object_count, sizeof(object_count)
				, objecv, objectc
				, 0);
		if(err) {
			// elevate to critical error
			err = log_critf("unhandled network error in jupdate/jselect: %d",
			                err);
			break;
		}

		if(!jselect) {
			// we're doing a jupdate, we have to read-back their response.
			err = edbs_jobread(job, &objecv, (int)objectc);
			if(err) {
				// elevate to critical error
				err = log_critf("unhandled network error in jupdate: %d",
				                err);
				break;
			}
		}

		// atp: we successfully sent this page to the client. And, if we're
		// doing a jupdate, received their response for changes.

		// checkout out the next page.
		err = edba_pageadvance(handle);
		if(err) {
			if(err == ODB_EEOF) {
				// we're done reading all the pages. Send a 0count to let
				// them know that.
				object_count = 0;
				edbs_jobwrite(job, &object_count, sizeof(object_count));
				break;
			}
			// something unexpected failed, send them a -1 to let them know
			// something critical happened.
			object_count = -1;
			edbs_jobwrite(job, &object_count, sizeof(object_count));
			break;
		}

		// succesfully checked out, now to process this page.
	}

	edba_pageclose(handle);
	return 0;
}

odb_err jselect(edb_worker_t *self) {
	return _jupdateselect(self, 1);
}

odb_err jupdate(edb_worker_t *self) {
	return _jupdateselect(self, 0);
}

// job data is assumed to be EDB_OBJ
odb_err edbw_u_objjob(edb_worker_t *self) {

	// easy pointers
	edbs_job_t job = self->curjob;
	int jobdesc = edbs_jobtype(job);

	// Now preform the actual object job.
	switch (jobdesc) {
		case ODB_JSELECT: return jselect(self);
		case ODB_JALLOC:  return jalloc(self);
		case ODB_JWRITE:  return jwrite(self);
		case ODB_JFREE:   return jfree(self);
		case ODB_JUPDATE: return jupdate(self);
		case ODB_JREAD:   return jread(self);
		default:          return log_critf("unknown job description passed in");
	}
}