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
		// later: need to find a way to roll-back the page creation here.
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
	pageheader->head.pleft = header.head.pleft;
	pageheader->head.pright = header.head.pright;

	// note: don't need to write the header, we can leave it as all 0s.

	// close the page
	edbp_finish(edbp);

	return 0;
}

edb_err edba_u_pagecreate_objects(edba_handle_t *handle,
                                  edbp_object_t header,
                                  const edb_struct_t *strct,
                                  uint8_t straitc, edb_pid *o_pid);
