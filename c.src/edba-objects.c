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
	// So lock the trashoffset on that page so we can load it.
	edba_u_locktrashstartoff(h, h->clutchedentry->trashlast);

	// check for trashfault
	edbp_start(&h->edbphandle, &h->clutchedentry->trashlast);
	edbp_object_t *o = edbp_gobject(&h->edbphandle);
	if(o->trashstart_off == (uint16_t)-1) {

		// if we're in here then what has happened is a trash fault.
		// See spec for details

		// trashfault, update trashlast
		h->clutchedentry->trashlast = o->trashvor;

		// it may seem compulsory to at this time set this page's trashvor to 0
		// as a matter of neatness. However, doing so will dirty the page. And
		// a non-0 trashvor can do no damage. so long that we take it out of the
		// trash management.
		//opage->trashvor = 0;
		//edbp_mod(&self->edbphandle, EDBP_CACHEHINT, EDBP_HDIRTY);

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
	// (note to self: we still have the XL lock on trashstart_off at this time)
	// (note to self: we know that opage is not -1 because trash faults have been handled)
	edb_pid pageid = h->clutchedentry->trashlast;
	edba_u_entrytrashunlk(h);

	// at this point, we have a lock on the trashstart_off and need
	// to place another lock on the actual trash record.
	// Store this in the h->lock so we can unlock when edba_objectclose is called
	edb_struct_t *structdat;
	edbd_struct(h->parent->descriptor, h->clutchedentry->structureid, &structdat);
	uint16_t intrapagebyteoff = EDBP_HEADSIZE + structdat->fixedc * o->trashstart_off;
	h->lock = (edbl_lockref) {
		.l_type = EDBL_EXCLUSIVE,
		.l_len = structdat->fixedc,
		.l_start = edbp_pid2off(h->parent->pagecache, pageid) + intrapagebyteoff,
	};
	edbl_set(&h->lockh, h->lock);

	// at this point, we have the deleted record locked as well as the
	// trashlast. So as per spec we must update trashstart and unlock
	// it but retain the lock on the record.

	// get some pointers to the object data, we'll need these these
	// now to get the next item on the trash list to set to trashstartoff
	// before we can release it
	edbp_t *page = edbp_graw(&h->edbphandle);
	h->objectdata = page + intrapagebyteoff;
	h->objectc    = structdat->fixedc;

	// store the current trashoffset in the stack for oid calculation later.
	uint64_t intrapagerowoff = o->trashstart_off;

	// for a deleted record, the first 2 bytes after the uint32_t header
	// is a rowid of the next item.
	o->trashstart_off = *(uint16_t *)(h->objectdata + sizeof(uint32_t));
	// trashstart_off is updated. we can unlock it. note we still have our
	// h->lock on the entry itself.
	edba_u_locktransstartoff_release(h);

	// calculate the id
	*o_oid = ((edbp_object_t *)page)->head.pleft * (uint64_t)h->clutchedentry->objectsperpage + intrapagerowoff;

	// as per this function's description, and the fact we just removed it
	// from the trash management, we will make sure its not marked as deleted.
	uint32_t *objflags = (uint32_t *)(h->objectdata);
#ifdef EDB_FUCKUPS
	// analyze the flags to make sure we're allowed to create this.
	// If this record has fallen into the trash list and the flags
	// don't line up, thats a error on my part.
	// double check that this record is indeed trash.
	// Note these errors are only critical if we eneded up here via
	// a auto-id creation.
	if(!(*objflags & EDB_FDELETED) || (*objflags & EDB_FUSRLCREAT)) {
		log_critf("recovered a deleted record out of trash that was not marked as deletion. "
				  "This could cause for concern for a corrupted trash cycle.");
	}
#endif
	*objflags = *objflags & ~EDB_FDELETED; // set deleted flag as 0.

	// sense we just updated that object flag, set the page to be dirty.
	edbp_mod(&h->edbphandle, EDBP_CACHEHINT, EDBP_HDIRTY);
	return 0;
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
