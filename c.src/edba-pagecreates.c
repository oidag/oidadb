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

edb_err edba_u_lookupdeepright(edba_handle_t *handle) {
#ifdef EDB_FUCKUPS
	if(handle->clutchedentry->trashlast) {
		log_critf("trashlast must be 0");
		return EDB_ECRIT;
	}
#endif

	// easy ptrs
	edbphandle_t *edbp = &handle->edbphandle;
	edb_entry_t *entry = handle->clutchedentry;
	unsigned int entryid = handle->clutchedentryeid;
	edb_err err;
	// grab the structure
	edb_struct_t *structdat;
	edbd_struct(handle->parent->descriptor, entry->structureid, &structdat);

	// The next 2 paragraphs of code will create the object but it will not
	// actually reference them anywhere. You
	// may ask, "why don't you create them only right before you need them
	// instead of at the top of the function?"... furthermore you may wonder
	// "what happens if we end up with an error half way through and it becomes
	// impossible to reference these pages?"
	//
	// These are good points. Let me explain: the biggest reason why we must
	// create them right now is because we can only have 1 page loaded at a
	// time (see edbp_start). And we cannot create AND initialize a page while
	// we already have a page loaded (see edba_u_pagecreate_... family).
	//
	// So we're given the option: do we create AND intialize the pages at the start
	// of the function, or do we create the pages as we need them (ie: we create the object
	// pages only after we're ready to reference them in the look up) and then initialize
	// them at the end of the funtion?
	//
	// We choose the former option because: errors are unlikely here. And if there are
	// errors, yeah it'll be expensive to roll-back the pages, but any errors that
	// happen in here will indicate the user has bigger problems then a few page leaks.
	// Even then we can always clean them in the future
	//
	// It should also be noted, that (despite any errors), we know FOR CERTAIN that
	// at least a strait of object pages WILL
	// be created per the nature of this function.

	// before we start to dig into the lookups, lets first create the object
	// pages.
	// todo: make sure if this function returns error-like, these pages are
	//       deleted.
	edbp_object_t header;
	header.structureid = entry->structureid;
	header.entryid = entryid;
	//header.trashvor = handle->clutchedentry->trashlast; we know trashlast is 0...
	// ... so we set trashlast to the newly created pageid plus strait
	//     (see edba_u_pagecreate_objects comment)
	header.trashvor = 0;
	header.head.pleft = entry->ref0c;
	unsigned int straitc = 1 << (entry->memory & 0x000F);
	edb_pid newpid;
	err = edba_u_pagecreate_objects(handle, header, structdat, straitc, &newpid);
	if(err) {
		return err;
	}
	// **defer on error: edba_u_pagedelete(handle, newpid, straitc);

	// atp: we have the new object pages created at newpid to newpid + straitc, but
	// they're entirely unreferanced (so make sure to clean them up on failure).
	// In total: we have no trashlast, we have object pages, now we need to
	// first put them in the lookup and then update trashlast.

	// Access the deep-right lookup page that is most likely to have null slots
	// open. Eventually, we'll set selectedref to a pointer to a loaded page
	// that is a null referance we can install our leaf (object pages)
	edb_pid lookuppid = entry->lastlookup;
	edbp_lookup_t *lookuphead;
	edb_lref_t *selectedref;
	int depth = entry->memory >> 0xC;

	// We are going to keep track of a few things across 2 loops.
	// The first loop will only loop if and only if the deep-righ
	// lookup is full. And if its parent is also full, it will loop
	// again, and so on.
	// The second loop will execute the exact amount of times the first
	// one did minus 1. The purpose of the second loop is to go back down
	// the tree to the leaf nodes (but only if the first one moved us up)
	//
	// The first loop will create lookup pages and store their PIDs in
	// newlookups. These pages are entirely unreferenced. The second
	// loop will then reference them.
	edb_pid newlookups[depth+1]; // will be empty if deep-right is currently spacious
	int i;
	for(i = depth; i >= 0; i++) { // note: '>=' because depth is 0-based

		// as per locking spec, we must place an XL lock on the second
		// byte on the page before we open it.
		edbl_lockref lock;
		lock.l_type = EDBL_EXCLUSIVE;
		lock.l_start = edbp_pid2off(edbp->parent, lookuppid) + 1;
		lock.l_len = 1;
		edbl_set(&handle->lockh, lock);
		lock.l_len = EDBL_TYPUNLOCK; // set for future use
		// **defer: edbl_set(&handle->lockh, lock);

		// load the page now that we have an XL lock
		err = edbp_start(edbp, lookuppid);
		if (err) {
			edbl_set(&handle->lockh, lock);
			goto rollbackcreatedpages;
		}
		// **defer: edbp_finish
		void *lookup = edbp_graw(edbp);
		lookuphead = lookup;
		edb_lref_t *lookuprefs = lookup + EDBP_HEADSIZE;

		// now we ask, is this page full?
		unsigned int refmax = handle->clutchedentry->lookupsperpage;
		if (lookuphead->refc == refmax) {
			// this page is full, we must add a new lookup page to
			// newlookups that will be utilized and refanced correctly
			// in the second loop going back down the tree.
			//
			// OR, if we're at the root lookup page and it is also full, that
			// means this entire tree is incabable of storing any more object pages.
			// ... so destroy all the lookup pages and object pages we created and
			// return EDB_ENOSPACE
			if(i == 0) {

				// the lowest level branch page is full, this means we
				// have no more space in the lookup tree.
				log_warnf("attempted to create object pages with full OID lookup tree: entry#%d", entryid);
				err = EDB_ENOSPACE;
				edbp_finish(edbp);
				edbl_set(&handle->lockh, lock);
				goto rollbackcreatedpages;
			}

			// this lookup page is full, but we have more branches above us.
			// So lets go up to the parent.
			lookuppid = lookuphead->parentlookup;
			// as per spec we deload and unlock this page.
			edbp_finish(edbp);
			edbl_set(&handle->lockh, lock);

			// todo: WAIT HOLDON... what if someone is putting in a new
			//       lookup reference in our parent at this very instant?
			//       should we lock the parent if we know we're about to
			//       create a sibling? hmmmmmmmmm
			edbp_lookup_t newheader;
			newheader.depth = i;
			newheader.entryid = entryid;
			// we know this newly created page is going to be
			// a sibling of the page we saw, so they'll have the
			// same parent.
			newheader.parentlookup = lookuppid;
			// we can't set pleft
			newheader.head.pleft = 0;
			newheader.head.pright = 0;
			edba_u_pagecreate_lookup()
			newlookups[i] =

			continue;
		} else {
			// todo: type out the logic here that describes what we do now that
			//       we found a lookup page that isn't full
		}
	}

	// the last loop may have sent us up the branches, if that's true, we
	// must navigate down. Both of these for loops is just a type of
	// recursion.
	//
	// Note to self: as per our previous comment, we increment 1 less than
	// what the above loop did. Because if the first loops code is executed
	// just once, that means the deep right was empty and no lookups need
	// to be created, but if it executed twice, that means 1  lookup page
	// must be referenced, and so one.
	for(; i < depth; i++) {
		// atp: selectedref is pointing to a null ref in a non-leaf-bearing
		// lookup page. So we must use this reference to install the page
		// we created in the accending loop (pid stored in newlookups).

		// note to self: pages in newlookups will have their parentlookup
		// header already set. check that logic out to make sure you do
		// this in the right order.

		// note to self: pleft and pright are NOT set in the last loop.
		// good reason for it: concurrent processes

		// todo:
	}


	// atp: We have a lookup page locked (for writting) and loaded that has a null
	//      referance at lookuprefs[lookuphead->refc - 1]. Thus, we
	//      must create a new referance.
	// todo: make sure to increment refc!

	edb_lref_t *nullref = lookuprefs[lookuphead->refc - 1];
	//HMMMMMMMMMMMMMMMM how do we fill out this ref? We need to create
	// a new lookup page, yet we already have this page locked. Thus,
	// we must create that lookup page before we get in this loop.

	// atp: selectedref is a null reference and we have it XL locked.
	selectedref->ref = newpid;
	selectedref->startoff_strait = header.head.pleft + straitc;
	hmmmmm need to yet again confirm the above logic


	// todo: handle->clutchedentry->ref0c += straitc

	// todo: see defers. make sure selectedref is unlocked

	return 0;
	rollbackcreatedpages:
	// todo: roll back ALL the lookup pages in reverse order.
	//  and then the object pages
	asdfasdfasdfasdfasdfasfasdfasf
	// roll back the look page
	if (edba_u_pagedelete(handle, newpid, straitc)) {
		log_critf("page leak: pid %ld-%ld", newpid, newpid + straitc);
	}
	// roll back the object pages
	if (edba_u_pagedelete(handle, newpid, straitc)) {
		log_critf("page leak: pid %ld-%ld", newpid, newpid + straitc);
	}
	return err;
}

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
