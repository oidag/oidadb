#include <stddef.h>
#define _LARGEFILE64_SOURCE
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#include "include/ellemdb.h"
#include "edbl.h"
#include "edba.h"
#include "edbs.h"


typedef enum obj_searchflags_em {
	// exclusive lock on the object binary instead of shared
	OBJ_XL = 0x0001,

} obj_searchflags;

typedef struct obj_searchparams_st {

	// inputs:
	edb_worker_t   *self;
	edb_eid         entryid;
	uint64_t        rowid;
	obj_searchflags flags;

	// outputs:
	// all o_ params are optional, but all previous o_ params must be non-null
	// for a given o_ param to be written too.
	//
	//   - o_entrydat: the entry data pulled from entryid
	//   - o_structdata: the structure data pulled from structures
	//   - o_objectoff: the total amount of bytes offset from the start of the file
	//                  requires transversing the edbp_lookup btree
	//   - o_objectdat: a pointer to the object data itself. note that o_objectdat
	//                  will point to the start of the whole object (head/flags included)
	edb_entry_t   **o_entrydat;
	edb_struct_t  **o_structdata;
	off64_t        *o_objectoff;
	void          **o_objectdat;
} obj_searchparams;

// helper function to all execjob_obj... functions.
//
// This will install proper locks and get meta data needed to
// work with objects.
//
// make sure to run execjob_obj_post when done with the same arguments.
//
// returns 1 if something is wrong and you should close out of
// the job (return from your function). Logs and error reports all handled.
// If returns 1 then no need for execjob_obj_post.
//
int static execjob_obj_pre(obj_searchparams dat) {
	edb_err err;

	edb_worker_t *self = dat.self;
	edb_eid entryid = dat.entryid;
	uint64_t rowid = dat.rowid;

	// SH lock the entry
	// ** defer: edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
	edbl_entry(&self->lockdir, entryid, EDBL_TYPSHARED);

	// get the index entry
	if(!dat.o_entrydat) {
		return 0;
	}
	err = edbd_index(self->shm, entryid, dat.o_entrydat);
	if(err) {
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		log_errorf("the supplied edb_oid does not have a valid entryid: %d", entryid);
		edbw_jobwrite(self, &err, sizeof(err));
		return 1;
	}
	edb_entry_t *entrydat = *dat.o_entrydat;

	// get the structure data
	if(!dat.o_structdata) {
		return 0;
	}
	err = edbd_struct(self->shm,
	                  entrydat->structureid,
	                  dat.o_structdata);
	if (err) {
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		log_critf("failed to load structure information (%d) for some reqson for a valid entry: %d",
		          entrydat->structureid, entryid);
		err = EDB_ECRIT;
		edbw_jobwrite(self, &err, sizeof(err));
		return 1;
	}

	// get the page/byte offset for the rowid from the start of the chapter.
	if(!dat.o_objectoff) {
		return 0;
	}
	edb_pid pageoffset;
	edb_pid foundpage;
	edb_struct_t *structdata = *dat.o_structdata;
	pageoffset = rowid / entrydat->objectsperpage;
	if (pageoffset >= entrydat->ref0c) {
		// the page offset is larger than the amount of pages we have.
		// thus, this rowid is impossible.
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		log_errorf("the supplied edb_oid has a page offset that is too large: %ld", pageoffset);
		err = EDB_EEOF;
		edbw_jobwrite(self, &err, sizeof(err));
		return 0;
	}

	// now we know how many pages we need to go in. So we now need to go
	// into the lookup b+tree. So lets pull up that information.
	// the following info is extracted from spec.
	int btree_depth = entrydat->memory >> 3;

	// do the b-tree lookup
	err = rowoffset_lookup(self,
	                       btree_depth,
	                       entrydat->ref1,
	                       pageoffset,
	                       &foundpage);
	if(err) {
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		edbw_jobwrite(self, &err, sizeof(err));
		return 1;
	}

	// page with the row was found.

	// So we calculate all the offset stuff.
	// get the intrapage byte offset
	// use math to get the byte offset of the start of the row data
	unsigned int intrapage_byteoff = edbp_object_intraoffset(rowid,
	                                                         pageoffset,
	                                                         entrydat->objectsperpage,
	                                                         structdata->fixedc);
	*dat.o_objectoff = edbp_pid2off(self->cache, foundpage) + intrapage_byteoff;

	if(!dat.o_objectdat) {
		return 0;
	}
	// as per locking spec, need to place the lock on the data before we load the page.
	// install the SH lock as per Object-Reading
	// or install an XL lock as per Object-Writing
	edbl_lockref lock = (edbl_lockref) {
			.l_type  = EDBL_TYPSHARED,
			.l_start = (*dat.o_objectdat),
			.l_len   = structdata->fixedc,
	};
	if(dat.flags & OBJ_XL) {
		lock.l_type = EDBL_EXCLUSIVE;
	}
	// ** defer: edbl_set(&self->lockdir, lock);
	edbl_set(&self->lockdir, lock);
	lock.l_type = EDBL_TYPUNLOCK; // set this in advance for back-outs

	// lock the page in cache
	err = edbp_start(&self->edbphandle, &foundpage);
	if(err) {
		edbl_set(&self->lockdir, lock);
		edbl_entry(&self->lockdir, entryid, EDBL_TYPUNLOCK);
		log_critf("unhandled error %d", err);
		edbw_jobwrite(self, &err, sizeof(err));
		return 1;
	}

	// parse the page into an edbp_object page
	edbp_object_t *o = edbp_gobject(&self->edbphandle);

	// get the offset for the record
	*dat.o_objectdat = o + intrapage_byteoff;

	return 0;
}
void static execjob_obj_post(obj_searchparams dat) {
	edb_worker_t *self = dat.self;

	// finis the page. This function will execute all the defers in the _pre function
	if(dat.o_objectdat) {
		// let the cache know we're done with the page
		edbp_finish(&self->edbphandle);
		// let the traffic control know we're done with the record
		edbl_lockref lock = (edbl_lockref) {
				.l_type  = EDBL_TYPUNLOCK,
				.l_start = (*dat.o_objectoff),
				.l_len   = (*dat.o_structdata)->fixedc,
		};
		edbl_set(&self->lockdir, lock);
	}
	// let the traffic control know we're done with the entry
	edbl_entry(&self->lockdir, dat.entryid, EDBL_TYPUNLOCK);
}


// copies the object (not including dynamic data) from the
// database to the job buffer.
//
// returns errors:
edb_err execjob_objcopy(edb_worker_t *self, edb_eid entryid, uint64_t rowid, void *dest) {

	// easy pointers
	edb_job_t *job = self->curjob;

	// first find the object we need based of the ID from the transfer buffer
	edb_err err = 0;

	edb_entry_t *entrydat;
	edb_struct_t *structdata;
	uint64_t dataoff;
	edb_pid pageoffset;
	void *recorddata;
	obj_searchparams dat = {0};
	dat = (obj_searchparams){
			self,
			entryid,
			rowid,
			0,
			&entrydat,
			&structdata,
			&dataoff,
			&recorddata
	};
	err = execjob_obj_pre(dat);
	if(err) {
		return;
	}

	// get the flags
	uint32_t flags = *(uint32_t *)recorddata;
	void     *body  = recorddata + sizeof(uint32_t); // plus uint32_t to get past the flags
	// is it deleted?
	if(flags & EDB_FDELETED) {
		err = EDB_ENOENT;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}
	// is it locked?
	if(flags & EDB_FUSRLRD) {
		err = EDB_EULOCK;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}

	// its all good.
	err = 0;
	edbw_jobwrite(self, &err, sizeof(err));

	// throw it all in the transfer buffer
	edbw_jobwrite(self, body, structdata->fixedc);

	finishpage:
	execjob_obj_post(dat);
}

static void execjob_objcreate(edb_worker_t *self, edb_eid entryid, uint64_t rowid) {

	// easy vars
	edb_job_t *job = self->curjob;
	edb_err err = 0;

	edb_entry_t *entrydat;
	edb_struct_t *structdata;
	obj_searchparams dat = (obj_searchparams){
			self,
			entryid,
			rowid,
			0,
			&entrydat,
			&structdata,
			0,0 // we'll find these manually.
	};
	// **defer: (label finishentry)
	err = execjob_obj_pre(dat);
	if(err) {
		return;
	}

	// todo: fuck I forgot about manual creation.

	// todo:
	//  - figure out how EDB_OID_AUTOID will seek to its ID
	//  - figure out if there needs to be another page created
	//  - and when a page is created, make sure all locks are set in the lookups.
	//  - also make sure if any new lookups need to be created.

	// We can look at the entry's trash start variable to get our
	// page id.
	// **defer: edbl_entrytrashlast(&self->lockdir, entryid, EDBL_TYPEUNLOCK);
	edbl_entrytrashlast(&self->lockdir, entryid, EDBL_EXCLUSIVE);

	// some vars to declare before we go into the trash logic loop.
	edbp_object_t *opage = 0;
	edb_pid trashlast;
	edbl_lockref lock_trashoff;

	loadtrashlast:
	if(!entrydat->trashlast) {
		// no valid trash pages. We must create new pages.
		// grab the last available lookuppage
		// todo: prepare lookup table
		uint64_t lookuppage = entrydat->lastlookup;
		// as per spec, place an XL lock on second byte
		edbl_lockref lock_lookup = (edbl_lockref){
				.l_type = EDBL_EXCLUSIVE,
				.l_start = edbp_pid2off(self->cache, lookuppage) + 1,
				.l_len = 1,
		};
		// **defer: edbl_set(&self->lockdir, lock_lookup);
		edbl_set(&self->lockdir, lock_lookup);
		lock_lookup.l_type = EDBL_TYPUNLOCK; // for future

		// **defer: edbp_finish
		edbp_start(&self->edbphandle, &lookuppage);



		entrydat->lookupsperpage
		asdf

		// XL lock on second byte as per spec
		uint16_t mps = 2^(entrydat->memory & 0x000F); // see spec on memory settings

		// initialize blank object pages. Their trashvors are all set so we can easily
		// update trashlast to the first element.
		err = edbp_createobj(&self->edbphandle, mps, &entrydat->trashlast);
		if(err) {
			// these should return the EDB_ENOSAPCE
			edbl_set(&self->lockdir, lock_lookup);
			edbp_finish(&self->edbphandle);
			edbw_jobwrite(self, &err, sizeof(err));
			edbl_entrytrashlast(&self->lockdir, entryid, EDBL_TYPUNLOCK);
			goto finishentry;
		}
		edbl_set(&self->lockdir, lock_lookup);
		edbp_finish(&self->edbphandle);

	}
	trashlast = entrydat->trashlast;
	// lock the page's header.trashstart_off as per spec
	off64_t pagebyteoff  = edbp_pid2off(self->cache, trashlast) +
	                       (off64_t)offsetof(edbp_object_t, trashstart_off);
	lock_trashoff = (edbl_lockref){
			.l_type = EDBL_EXCLUSIVE,
			.l_start = pagebyteoff,
			.l_len   = sizeof(uint16_t)
	};
	// **defer: edbl_set(&self->lockdir, lock_trashoff);
	edbl_set(&self->lockdir, lock_trashoff);
	lock_trashoff.l_type = EDBL_TYPUNLOCK; // for the future
	// **defer edbp_finish(&self->edbphandle)
	edbp_start(&self->edbphandle, &trashlast);
	opage = edbp_gobject(&self->edbphandle);
	if(opage->trashstart_off == (uint16_t)-1) {

		// if we're in here then what has happened is a trash fault.
		// See spec for details

		// take note of the trash vor, and release the page
		uint64_t trashvor = opage->trashvor;

		// it may seem compulsory to at this time set this page's trashvor to 0
		// as a matter of neatness. However, doing so will dirty the page. And
		// a non-0 trashvor can do no damage. so long that we take it out of the
		// trash management.
		//opage->trashvor = 0;
		//edbp_mod(&self->edbphandle, EDBP_CACHEHINT, EDBP_HDIRTY);

		// finish off this current page sense we don't need it anymore.
		edbp_finish(&self->edbphandle);
		edbl_set(&self->lockdir, lock_trashoff);

		// update the entry's trash last to the page's trashvor.
		// we then go back to where we loaded the trashvor and repeat the process.
		entrydat->trashlast = trashvor;
		goto loadtrashlast;
	}

	// now that we've loaded in our pages with a good trash managment loop,
	// we release the entry trash lock as per spec sense we have
	// no reason to update it at this time.
	// (note to self: we still have the XL lock on trashstart_off at this time)
	edbl_entrytrashlast(&self->lockdir, entryid, EDBL_TYPUNLOCK);

	// note: we know that opage is not -1 because trash faults have been
	// handled. The page we've loaded definitely has a valid opage.
	unsigned int intrapage_byteoff = opage->trashstart_off * structdata->fixedc;
	off64_t dataoff = edbp_pid2off(self->cache, trashlast) + intrapage_byteoff;

	// at this point, we have a lock on the trashstart_off and need
	// to place another lock on the actual trash record.
	edbl_lockref lock_record = (edbl_lockref ) {
			.l_type = EDBL_EXCLUSIVE,
			.l_start = dataoff,
			.l_len   = structdata->fixedc,
	};
	// **defer: edbl_set(&self->lockdir, lock_record);
	edbl_set(&self->lockdir, lock_record);
	lock_record.l_type = EDBL_TYPUNLOCK; // for future use

	// at this point, we have the deleted record locked as well as the
	// trashstart. So as per spec we must update trashstart and unlock
	// it but retain the lock on the record.
	void *objectdat = opage + (opage->trashstart_off);
	uint32_t *flags = (uint32_t *)objectdat;
	opage->trashstart_off = *(uint16_t *)(objectdat +
	                                      sizeof(uint32_t)); // + uint32 because thats the object head
#ifdef EDB_FUCKUPS
	{
		// analyze the flags to make sure we're allowed to create this.
		// If this record has fallen into the trash list and the flags
		// don't line up, thats a error on my part.
		// double check that this record is indeed trash.
		// Note these errors are only critical if we eneded up here via
		// a auto-id creation.
		if(!(*flags & EDB_FDELETED) || *flags & EDB_FUSRLCREAT) {
			log_critf("trash management logic has led to a row that is not not valid trash");
			err = EDB_ECRIT;
			edbw_jobwrite(self, &err, sizeof(err));
			edbl_set(&self->lockdir, lock_record);
			edbl_set(&self->lockdir, lock_trashoff);
			goto finishpage;
		}
	}
#endif
	edbl_set(&self->lockdir, lock_trashoff);

	// now have only a lock on the trash record, at this point there's
	// no turning back, the record is no longer considered trash.

	// successful call. send the caller a success reply.
	err = 0;
	edbw_jobwrite(self, &err, sizeof(err));
	*flags = *flags & ~EDB_FDELETED; // set deleted flag as 0.

	// write the record.
	edbw_jobread(self, objectdat, structdata->fixedc);

	finishpage:
	// we wrote to this page. So mark it as dirty before we detach
	edbp_mod(&self->edbphandle, EDBP_CACHEHINT, EDBP_HDIRTY);
	edbp_finish(&self->edbphandle);
	finishentry:
	// this will release the entry locks.
	execjob_obj_post(dat);

}

void static execjob_objedit(edb_worker_t *self, edb_eid entryid, uint64_t rowid) {

	// easy vars
	edb_job_t *job = self->curjob;

	edb_err err = 0;

	// get additional parameters
	uint32_t start,end;
	int ret;
	ret = edbw_jobread(self, &start, sizeof(start));
	ret += edbw_jobread(self, &end, sizeof(end));
	if(ret != sizeof(uint32_t)*2) {
		err = EDB_EHANDLE;
		edbw_jobwrite(self, &err, sizeof(err));
		return;
	}
	if(start >= end) {
		err = EDB_EINVAL;
		edbw_jobwrite(self, &err, sizeof(err));
		return;
	}

	edb_entry_t *entrydat;
	edb_struct_t *structdata;
	uint64_t dataoff;
	void *recorddata;
	obj_searchparams dat = (obj_searchparams){
			self,
			entryid,
			rowid,
			OBJ_XL,
			&entrydat,
			&structdata,
			&dataoff,
			&recorddata
	};
	err = execjob_obj_pre(dat);
	if(err) {
		return;
	}

	// check for flags
	// was it deleted?
	uint32_t flags = *(uint32_t *)recorddata;
	void     *body  = recorddata + sizeof(uint32_t); // plus uint32_t to get past the flags
	if(flags & EDB_FDELETED) {
		err = EDB_ENOENT;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}
	// is it write-locked?
	if(flags & EDB_FUSRLWR) {
		err = EDB_EULOCK;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}

	// clamp parameters
	if(start > structdata->fixedc) {
		err = EDB_EOUTBOUNDS;
		edbw_jobwrite(self, &err, sizeof(err));
		goto finishpage;
	}
	if(end > structdata->fixedc) {
		end = structdata->fixedc;
	}

	// its all good.
	err = 0;
	edbw_jobwrite(self, &err, sizeof(err));

	// read in the data
	edbw_jobread(self, body + start, (int)(end - start));

	// sense we just modified the page then hint that its dirty.
	edbp_mod(&self->edbphandle, EDBP_CACHEHINT, EDBP_HDIRTY);

	finishpage:
	execjob_obj_post(dat);
}
