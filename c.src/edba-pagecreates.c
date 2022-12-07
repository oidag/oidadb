#include "edba-util.h"
#include "edba.h"
#include "edbp-types.h"

// Okay now listen here, sunny. We must now create a bunch of new
// pages. And then we must remember that crashes can happen at
// any line. We don't want to end up with entries pointing to
// half-made pages.
//
// So its important that we do the following order:
//  1. create the page
//  2. write the page
//  3. set the page's type to what it must be
//  4. update the entry with the page's id.
// This will also be easy to pick out corrupted pages because their type will be EDB_TINIT.

edb_err edba_u_lookupdeepright(edba_handle_t *handle);
/*
for edba_u_lookupdeepright:
edbp_object_t obj_header;
obj_header.structureid = e.structureid;
// note: for trash management we don't need to worry about trash locks
//       because as we're creating this we have then entire entry XL
//       locked.
obj_header.trashvor = 0;
obj_header.head.pleft = 0;
e.ref0c = mps_objp;
edba_u_pagecreate_objects(h, obj_header, strck, mps_objp, &e.ref0);
e.trashlast = e.ref0;*/

edb_err edba_u_pagecreate_lookup(edba_handle_t *handle,
								 edbp_lookup_t header,
								 edb_pid *o_pid) {

	// easy ptrs
	edbphandle_t *edbp = &handle->edbphandle;
	edb_err err;

	// later: need to reuse deleted pages rather than creating them by utilizing the
	//        deleted page / trash line

	// create the page
	err = edbp_create(edbp, 1, o_pid);
	if(err) {
		return err;
	}

	// start the page
	err = edbp_start(edbp, *o_pid);
	if(err) {
		log_debugf("failed to start page after it's creation, "
				   "to prevent if from going unrefanced, will be attempting to delete it.");
		if(edba_u_pagedelete(handle, *o_pid, 1)) {
			log_critf("page leak! pid %ld is unreferanced", *o_pid);
		}
		return err;
	}
	// **defer: edbp_finish(edbp);
	void *page = edbp_graw(edbp);
	edbp_lookup_t *pageheader = (edbp_lookup_t *)page;

	// write the header
	pageheader->entryid = header.entryid;
	pageheader->refc    = 0;
	pageheader->parentlookup = header.parentlookup;
	pageheader->depth = header.depth;
	pageheader->head.ptype = EDB_TLOOKUP;
	pageheader->head.pleft = header.head.pleft;
	pageheader->head.pright = header.head.pright;

	// note: don't need to write the header, we can leave it as all 0s.

	// close the page
	edbp_finish(edbp);

	return 0;
}

void static initobjectspage(void *page, edbp_object_t header, const edb_struct_t *strct, unsigned int objectsperpage) {

	// set up the header
	edbp_object_t *phead = (edbp_object_t *)page;
	*phead = (edbp_object_t){
		.structureid = header.structureid,
		.entryid = header.entryid,
		.trashvor = header.trashvor,
		.trashc = objectsperpage,
		.trashstart_off = 0,

		.head.pleft = header.head.pleft,
		.head.pright = 0,
		.head.ptype = EDB_TOBJ,
		.head.rsvd = 0,
	};

	// set up the body
	void *body = page + EDBP_HEADSIZE;
	for(int i = 0; i < objectsperpage; i++) {
		void *obj = body + strct->fixedc * i;
		edb_object_flags *flags = obj;
		// mark them as all deleted. And daisy chain the trash
		// linked list.
		*flags = EDB_FDELETED;
		uint16_t *nextdeleted_rowid = obj + sizeof(edb_object_flags);
		if(i + 1 == objectsperpage) {
			// last one, set to -1.
			*nextdeleted_rowid = (uint16_t)-1;
		} else {
			*nextdeleted_rowid = ((uint16_t)i)+1;
		}
	}
}

edb_err edba_u_pagecreate_objects(edba_handle_t *handle,
                                  edbp_object_t header,
                                  const edb_struct_t *strct,
                                  uint8_t straitc, edb_pid *o_pid) {
	// easy ptrs
	edbphandle_t *edbp = &handle->edbphandle;
	edb_err err;
	unsigned int objectsperpage = (edbp_size(handle->edbphandle.parent) - EDBP_HEADSIZE) / strct->fixedc;

	// later: need to reuse deleted pages rather than creating them by utilizing the
	//        deleted page / trash line

	// create the pages
	err = edbp_create(edbp, straitc, o_pid);
	if(err) {
		return err;
	}

	// now we must loop through each page and initalize it.
	for(int i = 0; i < straitc; i++) {
		// start the page
		err = edbp_start(edbp, *o_pid);
		if (err) {
			log_debugf("failed to start page after it's creation, "
			           "to prevent if from going unrefanced, will be attempting to delete it.");
			if (edba_u_pagedelete(handle, *o_pid, 1)) {
				log_critf("page leak! pid %ld is unreferanced", *o_pid);
			}
			return err;
		}
		// **defer: edbp_finish(edbp);
		void *page = edbp_graw(edbp);
		initobjectspage(page, header, strct, objectsperpage);
		// we do the referance  logic after so the first iteration of
		// initobjectspage uses the trashvor/pleaft supplied by the caller of this
		// function.
		header.trashvor = *o_pid + i;
		edbp_finish(edbp);
	}

	return 0;
}
