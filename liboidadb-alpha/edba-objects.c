#include <stddef.h>
#define _LARGEFILE64_SOURCE
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#include <oidadb/oidadb.h>
#include "edbl.h"
#include "edba.h"
#include "edba_u.h"
#include "edbs.h"
#include <oidadb-internal/odbfile.h>

static void inline assignobject(edba_handle_t *h,
								void *page,
								uint16_t intrapagebyteoff,
								const odb_spec_struct_struct *structdat) {
	h->objectrowoff  = (intrapagebyteoff - ODB_SPEC_HEADSIZE) / structdat->fixedc;
#ifdef EDB_FUCKUPS
	if((intrapagebyteoff - ODB_SPEC_HEADSIZE) % structdat->fixedc != 0) {
		log_critf("assignobject called with an intra-page byte offset not "
				  "aligned to the start of a row");
	}
#endif
	h->objectc       = structdat->fixedc;
	h->objectflags   = page + intrapagebyteoff;
	h->dy_pointersc = structdat->data_ptrc;
	h->dy_pointers  = (void *)h->objectflags + sizeof(odb_spec_object_flags);
	h->contentc   = structdat->fixedc
	                - sizeof(odb_spec_object_flags)
	                - (sizeof(odb_dyptr) * h->dy_pointersc);
	h->content    = (void *)h->objectflags
	                + sizeof(odb_spec_object_flags)
	                + (sizeof(odb_dyptr) * h->dy_pointersc);
}

odb_err edba_objectopen(edba_handle_t *h, odb_oid oid, edbf_flags flags) {
	odb_eid eid;
	odb_rid rid;
	odb_err err;

	// handle-status poltiics.
	if(h->opened != 0) {
		log_critf("cannot open object, something already opened");
		return ODB_ECRIT;
	}
	h->opened = ODB_ELMOBJ;
	h->openflags = flags;

	// cluth lock the entry
	edba_u_oidextract(oid, &eid, &rid);
	err = edba_u_clutchentry(h, eid, 0);
	if(err) {
		return err;
	}

	// get the struct data
	const odb_spec_struct_struct *structdat;
	edbd_struct(h->parent->descriptor, h->clutchedentry->structureid, &structdat);

	// get the chapter offset
	odb_pid chapter_pageoff;
	uint16_t page_byteoff;
	edba_u_rid2chptrpageoff(h, h->clutchedentry, rid, &chapter_pageoff, &page_byteoff);

	// do the oid lookup
	odb_pid foundpid;
	err = edba_u_lookupoid(h, h->clutchedentry, chapter_pageoff, &foundpid);
	if(err) {
		edba_u_clutchentry_release(h);
		return err;
	}

	// load the page and put the row data in the handle
	err = edba_u_pageload_row(h, foundpid,
			page_byteoff, structdat, flags);
	if(err) {
		edba_u_clutchentry_release(h);
		return err;
	}

	// note to self: at this point we have the page loaded and the entry
	// shared-clutched locked.
	return 0;
}

odb_err edba_objectopenc(edba_handle_t *h, odb_oid *o_oid, edbf_flags flags) {
	odb_eid eid;
	odb_rid rid;
	odb_err err;
	odb_pid trashlast;

	// politics
	if(h->opened != 0) {
		log_critf("cannot open object, something already opened");
		return ODB_ECRIT;
	}
	h->openflags = flags;

	// cluth lock the entry
	edba_u_oidextract(*o_oid, &eid, &rid);
	if(eid < EDBD_EIDSTART) {
		return ODB_EINVAL;
	}
	err = edba_u_clutchentry(h, eid, 0);
	if(err) {
		return err;
	}

	// xl lock on the trashlast.
	// **defer: edbl_set(h->lockh, EDBL_ARELEASE, EDBL_LENTTRASH, clutchedid)
	edbl_set(h->lockh, EDBL_AXL, (edbl_lock){
		.type = EDBL_LENTTRASH,
		.eid = h->clutchedentryeid,
	});

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
			edbl_set(h->lockh, EDBL_ARELEASE, (edbl_lock){
					.type = EDBL_LENTTRASH,
					.eid = h->clutchedentryeid,
			});
			edba_u_clutchentry_release(h);
			return ODB_ENOSPACE;
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
	odb_pid pageid = h->clutchedentry->trashlast;
	edbl_set(h->lockh, EDBL_AXL, (edbl_lock){
			.type = EDBL_LTRASHOFF,
			.object_pid = pageid,
	});

	// check for trashfault
	edbp_start(h->edbphandle, pageid);
	odb_spec_object *o = edbp_graw(h->edbphandle);
	if(o->trashstart_off == (uint16_t)-1) {

		// if we're in here then what has happened is a trash fault.
		// See spec for details

		// trashfault, update trashlast
		h->clutchedentry->trashlast = o->trashvor;

		// Although it will dirty the page, we must set the trashvor to this
		// page to 0 so that it knows it's not in the trash linked list. This
		// information is needed for when the page faces so many manual deletions
		// it needs to know if its in the entry-page-trash linked list to avoid
		// adding itself twice (which will break the linked list)
		o->trashvor = 0;
		edbp_mod(h->edbphandle, EDBP_CACHEHINT, EDBP_HDIRTY);

		// close this page because there's no trash on it.
		edbp_finish(h->edbphandle);
		// unlock the trashoff of this page.
		edbl_set(h->lockh, EDBL_ARELEASE, (edbl_lock){
				.type = EDBL_LTRASHOFF,
				.object_pid = pageid,
		});
		// retry the updated trashlast
		goto seektrashlast;
	}

	// at this point, we have a page started with its trashstart offset
	// locked and non-0. We can release the XL lock on the chapter's trashlast
	// sense we have no reason to update it anymore. (as per spec)
	// (note to self: we still have the XL lock on trashstart_off at this time)
	// (note to self: we know that opage is not -1 because trash faults have been handled)
	edbl_set(h->lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LENTTRASH,
			.eid = h->clutchedentryeid,
	});

	// at this point, we have a lock on the trashstart_off and need
	// to place another lock on the actual trash record.
	// Store this in the h->lock so we can unlock when edba_objectclose is called
	const odb_spec_struct_struct *structdat;
	edbd_struct(h->parent->descriptor, h->clutchedentry->structureid, &structdat);
	uint16_t intrapagebyteoff = ODB_SPEC_HEADSIZE + structdat->fixedc * o->trashstart_off;
	h->lock = (edbl_lock){
		.type = EDBL_LROW,
		.object_pid = pageid,
		.page_ioffset = intrapagebyteoff,
	};
	edbl_set(h->lockh, EDBL_ARELEASE, h->lock);

	// at this point, we have the deleted record locked as well as the
	// trashlast. So as per spec we must update trashstart and unlock
	// it but retain the lock on the record.

	// get some pointers to the object data, we'll need these these
	// now to get the next item on the trash list to set to trashstartoff
	// before we can release it
	void *page = edbp_graw(h->edbphandle);
	assignobject(h, page, intrapagebyteoff, structdat);


	// store the current trashoffset in the stack for oid calculation.
	uint64_t intrapagerowoff = o->trashstart_off;

	// for a deleted record, the first 2 bytes after the uint32_t header
	// is a rowid of the next item.
	o->trashstart_off = *(uint16_t *)(h->content);
	o->trashc--;
	// trashstart_off is updated. we can unlock it. note we still have our
	// h->lock on the entry itself.
	edbl_set(h->lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LTRASHOFF,
			.object_pid = pageid,
	});

	// calculate the id
	*o_oid = ((odb_spec_object *)page)->head.pleft * (uint64_t)h->clutchedentry->objectsperpage + intrapagerowoff;
	*o_oid = *o_oid | ((odb_oid)eid << 0x30);

	// as per this function's description, and the fact we just removed it
	// from the trash management, we will make sure its not marked as deleted.
	odb_spec_object_flags *objflags = h->objectflags;
#ifdef EDB_FUCKUPS
	// analyze the objectflags to make sure we're allowed to create this.
	// If this record has fallen into the trash list and the objectflags
	// don't line up, thats a error on my part.
	// double check that this record is indeed trash.
	// Note these errors are only critical if we eneded up here via
	// a auto-id creation.
	if(!(*objflags & EDB_FDELETED)) {
		log_critf("recovered a deleted record out of trash that was not marked as deletion. "
				  "This could cause for concern for a corrupted trash cycle.");
	}
#endif
	*objflags = *objflags & ~EDB_FDELETED; // set deleted flag as 0.

	// sense we just updated that object flag, set the page to be dirty.
	edbp_mod(h->edbphandle, EDBP_CACHEHINT, EDBP_HDIRTY);
	h->opened = ODB_ELMOBJ;
	return 0;
}

void    edba_objectclose(edba_handle_t *h) {
#ifdef EDB_FUCKUPS
	if(h->opened != ODB_ELMOBJ) {
		log_debugf("trying to close object when non opened.");
	}
#endif
	edba_u_pagedeload(h);
	edba_u_clutchentry_release(h);
	h->opened = 0;
}

odb_sid edba_objectstructid(edba_handle_t *h) {
	return h->clutchedentry->structureid;
}

const odb_spec_struct_struct *edba_objectstruct(edba_handle_t *h) {
	const odb_spec_struct_struct *ret;
	edbd_struct(h->parent->descriptor, edba_objectstructid(h), &ret);
	return ret;
}

struct odb_structstat edba_objectstructstat(edba_handle_t *h) {
	return edba_u_stk2stat(*edba_objectstruct(h), edba_objectstructid(h));
}

void   *edba_objectfixed(edba_handle_t *h) {
	if(!(h->openflags & EDBA_FWRITE)) {
		log_critf("edba_objectfixed without having EDBA_FWRITE flag");
		return 0;
	}
	return h->content;
}

const void *edba_objectfixed_get(edba_handle_t *h) {
	if((h->openflags & EDBA_FREAD) != EDBA_FREAD) {
		log_critf("edba_objectfixed_get without having EDBA_FREAD flag");
		return 0;
	}
	return h->content;
}

odb_usrlk edba_objectlocks_get(edba_handle_t *h) {
	return (*(odb_usrlk *)h->objectflags) & _EDB_FUSRLALL;
}

odb_err edba_objectlocks_set(edba_handle_t *h, odb_usrlk lk) {
#ifdef EDB_FUCKUPS
	// invals
	if(h->opened != ODB_ELMOBJ || !(h->openflags & EDBA_FWRITE)) {
		log_critf("attempt to set locks in read-only mode");
		return ODB_EINVAL;
	}
#endif
	// prevent unormalized values
	if((lk & _EDB_FUSRLALL) != lk) {
		log_errorf("invalid user lock according to normalization mask");
		return ODB_EINVAL;
	}
	odb_spec_object_flags *objflags = h->objectflags;

	// clear out whatever was there.
	*objflags = *objflags & ~_EDB_FUSRLALL;

	// set the new value
	*objflags = *objflags | lk;

	return 0;
}


// delete group
unsigned int edba_objectdeleted(edba_handle_t *h) {
#ifdef EDB_FUCKUPS
	if(h->opened != ODB_ELMOBJ) {
		log_critf("opened parameter was not ODB_ELMOBJ");
		return 0;
	}
#endif
	return (*(h->objectflags) & EDB_FDELETED);
}

// this function must be the same across all databases, all tables.
// must return the same result for any 2 inputs regardless.
//
//
//
// returns 0 for shouldn't really be added trash list.
// returns 1 for should be added to trash list
static inline int EDB_TRASHCRITCALITY(uint16_t trashcount, uint16_t total) {
	return trashcount > (total / 2);
}

odb_err edba_objectdelete(edba_handle_t *h) {
#ifdef EDB_FUCKUPS
	if(h->opened != ODB_ELMOBJ || !(h->openflags & EDBA_FWRITE)) {
		log_critf("opened parameter was not ODB_ELMOBJ or without write permissions");
		return ODB_EINVAL;
	}
#endif

	// redundancy check (required)
	if(edba_objectdeleted(h)) {
		log_infof("attempted to delete already deleted object: oid#%ld",
				  edbp_gpid(h->edbphandle));
		return 0;
	}

	// as per spec we must lock the trash field. We can do this with the page loaded
	// because we have the record itself XL locked.
	edbl_set(h->lockh, EDBL_AXL, (edbl_lock){
			.type = EDBL_LTRASHOFF,
			.object_pid = edbp_gpid(h->edbphandle),
	});
	// **defer: edbl_set

	// mark as deleted
	// We do this first just incase we crash by the time we get to the trash linked list
	// we'd rather have it marked as deleted so future query processes won't try to use it.
	// It'll be up to pmaint to finally put it in the trash linked list in that case.
	odb_spec_object_flags *objflags = h->objectflags;
	*objflags = *objflags | EDB_FDELETED;

	// delete all dynamic data. We'll 0-out the pointers as their deleted just incase
	// of a crash (see above logic with setting EDB_FDELETED first).
	{
		for(int i = 0; i < h->dy_pointersc; i++) {
			if(h->dy_pointers[i] == 0) continue;
			edba_u_dynamicdelete(h, h->dy_pointers[i]);
			h->dy_pointers[i] = 0;
		}
	}

	// get the header of the object page
	odb_spec_object *objheader = edbp_graw(h->edbphandle);
	int trashcrit1 = EDB_TRASHCRITCALITY(objheader->trashc, h->clutchedentry->objectsperpage);
	// add to the trash linked list
	*(uint16_t *)(h->content) = objheader->trashstart_off;
	objheader->trashstart_off = h->objectrowoff;
	objheader->trashc++;

	int trashcrit2 = EDB_TRASHCRITCALITY(objheader->trashc, h->clutchedentry->objectsperpage);
	edbl_set(h->lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LTRASHOFF,
			.object_pid = edbp_gpid(h->edbphandle),
	});
	if(trashcrit2 == 0) {
		return 0;
	}

	// later: see task "page strait awareness"

	// atp: we know this page has hit trash criticality. Now we must find out if this
	//      page is in the trash cycle yet. We can do this efficiently by checking the
	//      trashcrit delta. If the delta hasn't changed, we know we don't need
	//      to do anything.

	if(trashcrit1 == trashcrit2) {
		// we can assume it's already in/not in the trash cycle sense the
		// criticality hasn't changed sense the last time we called it.
		return 0;
	}

	// But even though we the delta changed, there's a small chance its
	// already where it needs to be (in terms of in/out of the trash cycle).
	if(objheader->trashvor != 0 ||
	   h->clutchedentry->trashlast == edbp_gpid(h->edbphandle)) {
		// oddly enough, this page is already inside of the trash cycle.
		// How did this page end up in the trash cycle if it just now hit
		// trash criticality when it wasn't prior?

		// It can happen when a fresh page is added to the trash cycle, and
		// several inserts take place to fill the page so its no longer
		// critical trash... lets say 1 past critical trash. It would still
		// be in the cycle because its a fresh page and will still have room
		// (in other words, it was created in the trash cycle but was never
		// taken out).
		// In this case, lets say instead of an insert, the next command
		// is a delete...
		// that would make the trash criticality have a delta value even
		// though we're already in the trash cycle.
		//
		// Thus we end up here.
		return 0;
	}

	// if this page MUST be added to trash list. We'll have to use strict locking as to
	// make sure no one touches this pages =trashvor= nor the entry's trashlast
	edbl_set(h->lockh, EDBL_AXL, (edbl_lock){
			.type = EDBL_LENTTRASH,
			.eid = h->clutchedentryeid
	});

	objheader->trashvor = h->clutchedentry->trashlast;
	h->clutchedentry->trashlast = edbp_gpid(h->edbphandle);
	edbl_set(h->lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LENTTRASH,
			.eid = h->clutchedentryeid
	});
	return 0;
}

odb_err edba_objectundelete(edba_handle_t *h) {
#ifdef EDB_FUCKUPS
	if(h->opened != ODB_ELMOBJ || !(h->openflags & EDBA_FWRITE)) {
		log_critf("opened parameter was not ODB_ELMOBJ or without write permissions");
		return ODB_EINVAL;
	}
#endif

	// redundancy check (required)
	if(!edba_objectdeleted(h)) {
		log_infof("attempted to un-delete already existing object: oid#%ld",
		          edbp_gpid(h->edbphandle));
		return 0;
	}

	// get the header of the object page
	void *objpage = edbp_graw(h->edbphandle);
	odb_spec_object *objheader = objpage;

#ifdef EDB_FUCKUPS
	if(objheader->trashc == 0 || objheader->trashstart_off == (uint16_t)-1) {
		log_critf("for some reason this page's header doesnt reflect the assumption of deletion");
		return ODB_ECRIT;
	}
#endif

	// See locking.org for all of this.
	edbl_set(h->lockh, EDBL_AXL, (edbl_lock){
			.type = EDBL_LENTTRASH,
			.object_pid = edbp_gpid(h->edbphandle)
	});
	// **defer: edbl_set

	// later: it may be adventagous to have a doubly-linked list here so we don't
	//        have to force our way through this page to remove a given object from
	//        the linked list

	// next paragraph is just removing us from the trash linked list.
	uint16_t ll_ref_after = *(uint16_t *)(h->content);
	uint16_t *ll_ref = &objheader->trashstart_off;
	int i;
	for(i = 0; i < objheader->trashc; i++) {
		if(*ll_ref == h->objectrowoff) {
			// ll_ref is now pointing to the object that is before us in the linked
			// list. So we we need to update this object's list to skip past us.
			*ll_ref = ll_ref_after;
			break;
		}
		ll_ref = (uint16_t *)(objpage
				+ ODB_SPEC_HEADSIZE
				+ (*ll_ref * h->objectc)
				+ sizeof(odb_spec_object_flags));
	}
#ifdef EDB_FUCKUPS
	if(i == objheader->trashc) {
		log_critf("failed to find object in trash list");
	}
#endif
	objheader->trashc--;
	
	// mark as live
	odb_spec_object_flags *objflags = h->objectflags;
	*objflags = *objflags & ~EDB_FDELETED;

	edbl_set(h->lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LENTTRASH,
			.object_pid = edbp_gpid(h->edbphandle)
	});
	return 0;
}

odb_err edba_u_pageload_row(edba_handle_t *h, odb_pid pid,
                            uint16_t page_byteoff, const odb_spec_struct_struct *structdat,
                            edbf_flags flags) {
	// as per locking spec, need to place the lock on the data before we load the page.
	// install the SH lock as per Object-Reading
	// or install an XL lock as per Object-Writing
	edbl_act lockaction = EDBL_ASH;
	h->lock = (edbl_lock) {
			.type = EDBL_LROW,
			.object_pid = pid,
			.page_ioffset = page_byteoff,
	};
	if(flags & EDBA_FWRITE) {
		lockaction = EDBL_AXL;
	}
	edbl_set(h->lockh, lockaction, h->lock);

	// lock the page in cache
	odb_err err = edbp_start(h->edbphandle, pid);
	if(err) {
		edbl_set(h->lockh, EDBL_ARELEASE, h->lock);
		return err;
	}

	// set the pointer
	void *page = edbp_graw(h->edbphandle);
	assignobject(h, page, page_byteoff, structdat);
	return 0;
}

void edba_u_pagedeload(edba_handle_t *h) {
	// finish the page
	edbp_finish(h->edbphandle);

	// release whatever lock we saved when we loaded the page
	edbl_set(h->lockh, EDBL_ARELEASE, h->lock);
}



typedef enum obj_searchflags_em {
	// exclusive lock on the object binary instead of shared
	OBJ_XL = 0x0001,

} obj_searchflags;


// returns the amount of bytes into the object page until the start of the given row.
static unsigned int intraoffset(uint64_t rowid, uint64_t pageoffset, uint16_t
objectsperpage, uint16_t fixedlen)
{
	unsigned int ret = ODB_SPEC_HEADSIZE + (unsigned int)(rowid - pageoffset * (uint64_t)objectsperpage) * (unsigned int)fixedlen;
#ifdef EDB_FUCKUPS
	if(ret > (ODB_SPEC_HEADSIZE + (unsigned int)objectsperpage * (unsigned int)fixedlen)) {
		log_critf("intraoffset calculation corruption: calculated byte offset (%d) exceeds that of theoretical maximum (%d)",
		          ret, ODB_SPEC_HEADSIZE + (unsigned int)objectsperpage * (unsigned int)fixedlen);
	}
#endif
	return ret;

}

void edba_u_rid2chptrpageoff(edba_handle_t *handle, odb_spec_index_entry *entrydat, odb_rid rowid,
                             odb_pid *o_chapter_pageoff,
                             uint16_t *o_page_byteoff) {
	const odb_spec_struct_struct *structdata;
	edbd_struct(handle->parent->descriptor, entrydat->structureid, &structdata);
	*o_chapter_pageoff = rowid / entrydat->objectsperpage;

	// So we calculate all the offset stuff.
	// get the intrapage byte offset
	// use math to get the byte offset of the start of the row data
	*o_page_byteoff = intraoffset(rowid,
	                              *o_chapter_pageoff,
	                              entrydat->objectsperpage,
	                              structdata->fixedc);
}
