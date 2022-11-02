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
#include "edba-util.h"
#include "edbs.h"
#include "edbp-types.h"

edb_err edba_objectopen(edba_handle_t *h, edb_oid oid, edbf_flags flags) {
	edb_eid eid;
	edb_rid rid;
	edb_err err;

	// cluth lock the entry
	edba_u_oidextract(oid, &eid, &rid);
	err = edba_u_clutchentry(h, eid);
	if(err) {
		return err;
	}

	// get the struct data
	edb_struct_t *structdat;
	edbd_struct(h->parent->descriptor, h->clutchedentry->structureid, &structdat);

	// get the chapter offset
	edb_pid chapter_pageoff;
	uint16_t page_byteoff;
	edba_u_rid2chptrpageoff(h, h->clutchedentry, rid, &chapter_pageoff, &page_byteoff);

	// do the oid lookup
	edb_pid foundpid;
	err = edba_u_lookupoid(h, h->clutchedentry, chapter_pageoff, &foundpid);
	if(err) {
		edba_u_clutchentry_release(h);
		return err;
	}

	// load the page
	err = edba_u_pageload_row(h, foundpid,
			page_byteoff, structdat->fixedc, flags);
	if(err) {
		edba_u_clutchentry_release(h);
		return err;
	}
}

edb_err edba_objectopenc(edba_handle_t *h, edb_oid *o_oid, edbf_flags flags) {
	edb_eid eid;
	edb_rid rid;
	edb_err err;
	edb_pid trashlast;

	// cluth lock the entry
	edba_u_oidextract(*o_oid, &eid, &rid);
	err = edba_u_clutchentry(h, eid);
	if(err) {
		return err;
	}

	// xl lock on the trashlast.
	// **defer: edba_u_entrytrashunlk
	edba_u_entrytrashlk(h);

#ifdef EDB_FUCKUPS
	int create_trashlast_check = 0;
#endif

	// read the trashlast
	seektrashlast:
	if(!h->clutchedentry->trashlast) {
		// if its 0 then we must make pages.
		// If we were not given EDBA_FCREATE, then we cannot make pages.
		// Otherwise we call edba_u_lookupdeepright which will create pages
		// and update trashlast.
		if(!(flags & EDBA_FCREATE) || edba_u_lookupdeepright(h)) {
			edba_u_entrytrashunlk(h);
			edba_u_clutchentry_release(h);
			return EDB_ENOSPACE;
		}
#ifdef EDB_FUCKUPS
		if(create_trashlast_check) {
			// if we're here that means we previously set create_trashlast_check = 1
			// yet 'goto seektrashlast' was still invoked due to trash fault
			log_critf("while auto creation id, pages were created yet a trash fault occured."
					  " When pages are added to trashlast in the same opperation as trashlast is discovered as 0, that means"
					  "trashlast should have been pointing to a blank page, thus no fault should be possible.");
		}
		create_trashlast_check = 1;
#endif
	}

	// at this point, we know trashlast is pointing to a leaf page.
	edba_u_locktrashstartoff(h, h->clutchedentry->trashlast);

	// check for trashfault
	edbp_start(&h->edbphandle, &h->clutchedentry->trashlast);
	edbp_object_t *o = edbp_gobject(&h->edbphandle);
	if(o->trashstart_off == (uint16_t)-1) {
		// trashfault, update trashlast
		h->clutchedentry->trashlast = o->trashvor;
		// close this page because there's no trash on it.
		edbp_finish(&h->edbphandle);
		// unlock the trashoff of this page.
		edba_u_locktransstartoff_release(h);
		// retry the updated trashlast
		goto seektrashlast;
	}

	// at this point, we have a page started with its trashstart offset
	// locked and non-0. We can release the XL lock on the chapter's trashlast
	// sense we have no reason to update it anymore. (as per spec)
	edb_pid pageid = h->clutchedentry->trashlast;
	edba_u_entrytrashunlk(h);

	// place our lock on the entry
	edb_struct_t *structdat;
	edbd_struct(h->parent->descriptor, h->clutchedentry->structureid, &structdat);
	uint16_t intrapagebyteoff = EDBP_HEADSIZE + structdat->fixedc * o->trashstart_off;
	h->lock = (edbl_lockref) {
		.l_type = EDBL_EXCLUSIVE,
		.l_len = structdat->fixedc,
		.l_start = edbp_pid2off(h->parent->pagecache, pageid) + intrapagebyteoff,
	};
	edbl_set(&h->lockh, h->lock);

	// now load in the object data
	h->objectdata = edbp_graw(&h->edbphandle) + intrapagebyteoff;
	h->objectc    = structdat->fixedc,

	// todo: shit. i just realize that we're dumping the dynamic data pointers into the streams
	// todo: output OID

	// lock aquired, as per spec, update trashstart_off and then release the lock
	// as per spec, for a deleted record, the first 2 bytes after the uint32_t header
	// is a rowid of the next item.
	o->trashstart_off = *(uint16_t *)(h->objectdata + sizeof(uint32_t));
	// trashstart_off is updated. we can unlock it. note we still have our
	// h->lock on the entry itself.
	edba_u_locktransstartoff_release(h);

	// as per this function's description, and the fact we just removed it
	// from the trash management, we will make sure its not marked as deleted.
	// todo: make some fuck up checks looking at the flags
	uint32_t *objflags = (uint32_t *)(h->objectdata);
	*objflags = *objflags & ~EDB_FDELETED; // set deleted flag as 0.

	// todo: compare this function with the old create function make sure notes line up

	// todo: make sure the only locks left are the h->lock and the clutch lock.
}

void    edba_objectclose(edba_handle_t *h) {
	edba_u_pagedeload(h);
	edba_u_clutchentry_release(h);
}

edb_err edba_u_pageload_row(edba_handle_t *h, edb_pid pid,
                         uint16_t page_byteoff, uint16_t fixedc,
                         edbf_flags flags) {
	// as per locking spec, need to place the lock on the data before we load the page.
	// install the SH lock as per Object-Reading
	// or install an XL lock as per Object-Writing
	h->lock = (edbl_lockref) {
			.l_type  = EDBL_TYPSHARED,
			.l_start = edbp_pid2off(h->parent->pagecache, pid) + page_byteoff,
			.l_len   = fixedc,
	};
	if(flags & EDBA_FWRITE) {
		h->lock.l_type = EDBL_EXCLUSIVE;
	}
	// todo: ** defer: edbl_set(&self->lockdir, lock);
	edbl_set(&h->lockh, h->lock);

	// lock the page in cache
	edb_err err = edbp_start(&h->edbphandle, &pid);
	if(err) {
		h->lock.l_type = EDBL_TYPUNLOCK;
		edbl_set(&h->lockh, h->lock);
		return err;
	}

	// set the pointer
	h->objectdata = edbp_graw(&h->edbphandle) + page_byteoff;
	h->objectc    = fixedc;

	return 0;
}

void edba_u_pagedeload(edba_handle_t *h) {
	// finish the page
	edbp_finish(&h->edbphandle);

	// release whatever lock we saved when we loaded the page
	h->lock.l_type = EDBL_TYPUNLOCK;
	edbl_set(&h->lockh, h->lock);
}



typedef enum obj_searchflags_em {
	// exclusive lock on the object binary instead of shared
	OBJ_XL = 0x0001,

} obj_searchflags;



edb_err edba_u_clutchentry(edba_handle_t *handle, edb_eid eid) {
	// SH lock the entry
	edbl_entry(&handle->lockh, eid, EDBL_TYPSHARED);
	edb_err err = edbd_index(handle->parent->descriptor, eid, &handle->clutchedentry);
	if(err) {
		edbl_entry(&handle->lockh, eid, EDBL_TYPUNLOCK);
		return err;
	}
	handle->clutchedentryeid = eid;
	return 0;
}
void edba_u_clutchentry_release(edba_handle_t *handle) {
	edbl_entry(&handle->lockh, handle->clutchedentryeid, EDBL_TYPUNLOCK);
}

void edba_u_rid2chptrpageoff(edba_handle_t *handle, edb_entry_t *entrydat, edb_rid rowid,
                             edb_pid *o_chapter_pageoff,
							 uint16_t *o_page_byteoff) {
	edb_struct_t *structdata;
	edbd_struct(handle->parent->descriptor, entrydat->structureid, &structdata);
	*o_chapter_pageoff = rowid / entrydat->objectsperpage;

	// So we calculate all the offset stuff.
	// get the intrapage byte offset
	// use math to get the byte offset of the start of the row data
	*o_page_byteoff = edbp_object_intraoffset(rowid,
	                                          *o_chapter_pageoff,
	                                          entrydat->objectsperpage,
	                                          structdata->fixedc);
}

static void execjob_objcreate(edb_worker_t *self, edb_eid entryid, uint64_t rowid) {

	// easy vars
	edb_job_t *job = self->curjob;
	edb_err err = 0;

	edb_entry_t *entrydat;
	edb_entry_t *structdata;
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
	edbl_entrytrashlast(&self->lockh, entryid, EDBL_EXCLUSIVE);

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
		edbl_set(&self->lockh, lock_lookup);
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
			edbl_set(&self->lockh, lock_lookup);
			edbp_finish(&self->edbphandle);
			edbw_jobwrite(self, &err, sizeof(err));
			edbl_entrytrashlast(&self->lockh, entryid, EDBL_TYPUNLOCK);
			goto finishentry;
		}
		edbl_set(&self->lockh, lock_lookup);
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
	edbl_set(&self->lockh, lock_trashoff);
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
		edbl_set(&self->lockh, lock_trashoff);

		// update the entry's trash last to the page's trashvor.
		// we then go back to where we loaded the trashvor and repeat the process.
		entrydat->trashlast = trashvor;
		goto loadtrashlast;
	}

	// now that we've loaded in our pages with a good trash managment loop,
	// we release the entry trash lock as per spec sense we have
	// no reason to update it at this time.
	// (note to self: we still have the XL lock on trashstart_off at this time)
	edbl_entrytrashlast(&self->lockh, entryid, EDBL_TYPUNLOCK);

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
	edbl_set(&self->lockh, lock_record);
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
			edbl_set(&self->lockh, lock_record);
			edbl_set(&self->lockh, lock_trashoff);
			goto finishpage;
		}
	}
#endif
	edbl_set(&self->lockh, lock_trashoff);

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
