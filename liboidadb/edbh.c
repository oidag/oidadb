#define _GNU_SOURCE

#include <malloc.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <stdatomic.h>
#include "edbh_u.h"
#include <oidadb/oidadb.h>
#include "edbh_u.h"
#include "edbh.h"
#include "errors.h"
#include "edbs.h"
#include "edbs-jobs.h"

// used to create warnings
// see https://gcc.gnu.org/onlinedocs/gcc/Thread-Local.html
static __thread int safety_threadwarn = 0;

odb_err odbh_index(odbh *handle
						  , odb_eid eid
						  , struct odb_entstat *o_entry) {
	if(!handle->indexv) {
		log_infof("downloading index...");
		struct odbh_jobret jr = odbh_jent_download(handle
				, &handle->indexv);
		if(jr.err) {

			// all errors returned by the download function we'll convert to
			// critical sense none of them are documented of this function
			if(jr.err != ODB_ECRIT) {
				log_critf("unexpected error from download");
				jr.err = ODB_ECRIT;
			}
			return jr.err;
		}
		handle->indexc = jr.length;
	}
	if(eid < EDBD_EIDSTART) {
		return ODB_ENOENT;
	}
	if(eid >= handle->indexc) {
		return ODB_EEOF;
	}
	*o_entry = handle->indexv[eid];
	return 0;
}

odb_err odbh_structs(odbh *handle
		, odb_sid structureid
		, struct odb_structstat *o_struct) {
	if(!handle->stkv) {
		log_infof("downloading structure index...");
		struct odbh_jobret jr = odbh_jstk_download(handle
				, &handle->stkv);
		if(jr.err) {

			// all errors returned by the download function we'll convert to
			// critical sense none of them are documented of this function
			if(jr.err != ODB_ECRIT) {
				log_critf("unexpected error from download");
				jr.err = ODB_ECRIT;
			}
			return jr.err;
		}
		handle->stkc = jr.length;
	}

	// check for ODB_EEOF
	if(structureid >= handle->stkc) {
		return ODB_EEOF;
	}

	// assign everything except for confv
	o_struct->fixedc = handle->stkv[structureid].fixedc;
	o_struct->confc = handle->stkv[structureid].confc;
	o_struct->dynmc = handle->stkv[structureid].dynmc;
	o_struct->objc = handle->stkv[structureid].objc;
	o_struct->svid  = handle->stkv[structureid].svid;

	return 0;
}

odb_err odbh_structs_conf(odbh *handle
		, odb_sid structureid
		, const struct odb_structstat *structstat) {
	odb_err err;

	if(!handle->stkv) {
		log_infof("downloading structure index...");
		struct odbh_jobret jr = odbh_jstk_download(handle
				, &handle->stkv);
		if(jr.err) {

			// all errors returned by the download function we'll convert to
			// critical sense none of them are documented of this function
			if(jr.err != ODB_ECRIT) {
				log_critf("unexpected error from download");
				jr.err = ODB_ECRIT;
			}
			return jr.err;
		}
		handle->stkc = jr.length;
	}

	// check for ODB_EEOF
	if(structureid >= handle->stkc) {
		return ODB_EEOF;
	}

	if(structstat->confc < handle->stkv[structureid].confc) {
		return ODB_EBUFFSIZE;
	}

	memcpy(structstat->confv
		   , handle->stkv[structureid].confv
		   , structstat->confc);
	return 0;
}

odb_err odb_handle(const char *path, odbh **o_handle) {

	// invals
	if(!path || !o_handle) {
		return ODB_EINVAL;
	}

	// provide a warning if they're creating multiple handles on the same
	// thread. See function documentation as to why this is bad.
	if(safety_threadwarn) {
		log_warnf("attempt to create multiple odbh instances via odb_handle "
				  "on the same thread: each thread should call odb_handle "
				  "independently");
	}

	// easy vars
	odb_err err;
	odbh *ret = malloc(sizeof(odbh));
	if(!ret) {
		if(errno == ENOMEM) {
			return ODB_ENOMEM;
		}
		return ODB_EERRNO;
	}
	bzero(ret, sizeof(odbh));

	// get pid.
	if((err = edb_host_getpid(path, &ret->hostpid))) {
		goto defer_dealloc;
	}

	// connect to shm
	if((err = edbs_handle_init(&ret->shm, ret->hostpid))) {
		goto defer_dealloc;
	}

	*o_handle = ret;
	atomic_fetch_add(&safety_threadwarn, 1);
	return 0;

	// defers:
	defer_shm:
	edbs_handle_free(ret->shm);
	defer_dealloc:
	free(ret);
	return err;
}

void odb_handleclose(odbh *handle) {
	// todo: should handle close implictly call odb_jclose just incase?
	if(handle->indexv) free(handle->indexv);
	if(handle->stkv) free(handle->stkv);
	edbs_handle_free(handle->shm);
	free(handle);
	atomic_fetch_sub(&safety_threadwarn, 1);
}

// helper method to all odb_jobj definitoins
// o_estat is optional, can be null
odb_err static eid2struct(odbh *handle
						  , odb_eid eid
						  , struct odb_structstat *o_stat
		, struct odb_entstat *o_estat) {
	struct odb_entstat useestat;
	if(!o_estat) {
		o_estat = &useestat;
	}
	odb_err err;
	if((err = odbh_index(handle, eid, o_estat))) {
		if(err != ODB_ECRIT) {
			// we will conclude any errors outside of a critical error can be
			// assumed to be used as this function's definition of ODB_ENOENT.
			err = ODB_ENOENT;
		}
		return err;
	}
	if((err = odbh_structs(handle, o_estat->structureid, o_stat))) {
		// yeah there should be no reason this returns an error.
		log_critf("unhandled error %d", err);
		err = ODB_ECRIT;
		return err;
	}
	return 0;
}

struct odbh_jobret odbh_jobj_alloc(odbh *handle
		, odb_eid eid
		, const void *usrobj) {

	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {ret.err=ODB_EINVAL; return ret;};
	if(usrobj == 0) {ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// get structure
	struct odb_structstat structstat;
	if((ret.err = eid2struct(handle, eid, &structstat, 0))) {
		return ret;
	}

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JALLOC, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the eid+objectdata
	if((ret.err = edbs_jobwritev(job
			, &eid, sizeof(eid)
			, &structstat.svid, sizeof(structstat.svid)
			, usrobj, structstat.objc
			, 0))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err+oid
	odb_err dieerr;
	if((ret.err = edbs_jobreadv(job
			, &dieerr, sizeof(dieerr)
			, &ret.oid, sizeof(ret.oid)
			, 0))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// successful execution
	return ret;
}


struct odbh_jobret odbh_jobj_free(odbh *handle
		, odb_oid oid) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JFREE, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the oid
	if((ret.err = edbs_jobwrite(job
			, &oid, sizeof(oid)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err
	odb_err dieerr;
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// successful execution
	return ret;
}

struct odbh_jobret odbh_jobj_write(odbh *handle
		, odb_oid oid
		, const void *usrobj) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {ret.err=ODB_EINVAL; return ret;};
	if(usrobj == 0) {ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// get structure
	struct odb_structstat structstat;
	if((ret.err = eid2struct(handle, odb_oid_get_eid(oid), &structstat, 0))) {
		return ret;
	}

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JWRITE, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the oid+svid+object
	if((ret.err = edbs_jobwritev(job
			, &oid, sizeof(oid)
			, &structstat.svid, sizeof(structstat.svid)
			, usrobj, structstat.objc
			, 0))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err
	odb_err dieerr;
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// successful execution
	return ret;
}

struct odbh_jobret odbh_jobj_read(odbh *handle
		, odb_oid oid
		, void *usrobj) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0) {ret.err=ODB_EINVAL; return ret;};
	if(usrobj == 0) {ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// get structure
	struct odb_structstat structstat;
	if((ret.err = eid2struct(handle, odb_oid_get_eid(oid), &structstat,0))) {
		return ret;
	}

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, ODB_JREAD, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the oid+SVID
	if((ret.err = edbs_jobwritev(job
			, &oid, sizeof(oid)
			, &structstat.svid, sizeof(structstat.svid)
			,0))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// read err+usrobj
	odb_err dieerr;
	if((ret.err = edbs_jobreadv(job
			, &dieerr, sizeof(dieerr)
			, usrobj, structstat.objc
			, 0))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// successful execution
	return ret;
}

// helper function to odbh_jobj_selectcb and odbh_jobj_updatecb. They share
// 90% of the same protocol.
struct odbh_jobret _odbh_jobj_cb(odbh *handle
			, odb_eid eid
			, odb_jobtype_t jobtype
			, void *cb) {
	struct odbh_jobret ret;
	edbs_job_t job;

	// invals
	if(handle == 0
	   || cb == 0)
	{ret.err=ODB_EINVAL; return ret;};

	// easy vars
	edbs_handle_t *shm = handle->shm;

	// get structure
	struct odb_structstat structstat;
	struct odb_entstat estat;
	if((ret.err = eid2struct(handle, eid, &structstat, &estat))) {
		return ret;
	}

	// install the job and check for ODB_EVERSION
	if((ret.err = edbs_jobinstall(handle->shm, jobtype, &job))) {
		if(ret.err == ODB_EJOBDESC) {
			ret.err = ODB_EVERSION;
		}
		return ret;
	}

	// write the eid/pagestart/pagecap
	// later: have page_start and page_cap avaialbe to the user for
	//  multi-processing.
	odb_pid page_start = 0,page_cap = -1;
	if((ret.err = edbs_jobwritev(job
			, &eid, sizeof(eid)
			, &page_start, sizeof(page_start)
			, &page_cap, sizeof(page_cap)
			, 0))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}

	// check for error+objc
	uint32_t objc;
	odb_err dieerr;
	// later: I should probably start multiple jobs here using the most
	//  amount of reasonable threads so that the host is sending us as many
	//  pages as possible. Though keep in mind we are always going to be
	//  bottle necked by our network interface. Testing is required to see if
	//  multithreading this job is worth wile.
	if((ret.err = edbs_jobread(job
			, &dieerr, sizeof(dieerr)))) {
		ret.err = edbs_joberr_trunc(ret.err);
		return ret;
	}
	if(dieerr) {
		ret.err = dieerr;
		return ret;
	}

	// now we have a stream of objects
	uint64_t userbuffq = PAGE_SIZE;
	void *userobjbuff = malloc(PAGE_SIZE);
	while(1) {
		if ((ret.err = edbs_jobread(job, &objc, sizeof(objc)))) {
			ret.err = edbs_joberr_trunc(ret.err);
			break;
		}

		// if we had read 0 objc, that means we've read all the objects and
		// we have no more objects to send to the callback. This we can also
		// assume the whole job was a success so our ret.err is 0.
		if(objc == 0) {
			ret.err = 0;
			break;
		}


		// the special case that the server returns -1 means a critical error
		// has happened that prevents it from sending us another payload.
		if(objc == -1) {
			ret.err = log_critf("host broadcasted critical error in "
								"select/update statement");
			break;
		}

		// make sure our buffer is big enough to receive this amount of objects.
		uint64_t payload = objc * sizeof(structstat.fixedc);
		if(payload > userbuffq) {
			free(userobjbuff);
			userobjbuff = malloc(payload);
			userbuffq = payload;
			if(userobjbuff == 0) {
				log_critf("failed to read from stream: out of memory");
				ret.err = ODB_ECRIT;
				break;
			}
		}

		// read in the objects from the stream
		if ((ret.err = edbs_jobread(job, &userobjbuff, sizeof(payload)))) {
			ret.err = edbs_joberr_trunc(ret.err);
			break;
		}

		// todo: these callbacks will be exposed the ENTIRE object...
		//  including dynamics and flags. Need to make sure its documented.

		if(jobtype == ODB_JUPDATE) {
			odb_update_cb *cb_ = cb;
			cb_(handle->user_cookie, objc, userobjbuff);
		} else {
			// here we its assume its ODB_JSELECT
			odb_select_cb *cb_ = cb;
			cb_(handle->user_cookie, objc, userobjbuff);
		}

		// now if we're executing an update then as per spec we must reply
		// with our updates.
		if(jobtype == ODB_JUPDATE) {
			//sense we're updating we must send our changes back to the host.
			if ((ret.err = edbs_jobwrite(job, &userobjbuff, sizeof(payload)))) {
				ret.err = edbs_joberr_trunc(ret.err);
				break;
			}
		}
	}
	free(userobjbuff);
	return ret;
}

struct odbh_jobret odbh_jobj_selectcb(odbh *handle
		, odb_eid eid
		, odb_select_cb cb) {
	return _odbh_jobj_cb(handle, eid, ODB_JSELECT, cb);
}

struct odbh_jobret odbh_jobj_updatecb(odbh *handle
		, odb_eid eid
		, odb_update_cb cb) {
	return _odbh_jobj_cb(handle, eid, ODB_JUPDATE, cb);
}

