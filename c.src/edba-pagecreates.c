#include "edba_u.h"
#include "edba.h"
#include "odb-structures.h"
#include "telemetry.h"

#include <strings.h>

// helper function
static odb_err xlloadlookup(edba_handle_t *handle,
                            edb_pid lookuppid,
                            edbl_lock *lock,
                            odb_spec_lookup **lookuphead,
                            odb_spec_lookup_lref **lookuprefs) {
	odb_err err;
	edbphandle_t *edbp = handle->edbphandle;

	// as per locking spec, we must place an XL lock on the second
	// byte on the page before we open it.
	lock->type = EDBL_LLOOKUP_NEW;
	lock->lookup_pid = lookuppid;
	edbl_set(handle->lockh, EDBL_AXL, *lock);
	// **defer: edbl_set(&handle->lockh, lock);

	// load the page now that we have an XL lock
	err = edbp_start(edbp, lookuppid);
	if (err) {
		edbl_set(handle->lockh, EDBL_ARELEASE, *lock);
		return err;
	}
	// **defer: edbp_finish
	// get the void pointer of the existing lookup page
	void *lookup = edbp_graw(edbp);
	*lookuphead = lookup;
	*lookuprefs = lookup + ODB_SPEC_HEADSIZE;

	// we can set this hint now to save us from doing it on the
	// several exits.
	edbp_mod(edbp, EDBP_CACHEHINT, EDBP_HINDEX3 * (4-(*lookuphead)->depth));
	return 0;
}

// safe to call twice / without having a load
//
// It will set a log message or few, so only use the call-twice in
// returning critical errors.
static void deloadlookup(edba_handle_t *handle,
                         edbl_lock *lock) {
	edbp_finish(handle->edbphandle);
	edbl_set(handle->lockh, EDBL_ARELEASE, *lock);
}

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
// This will also be easy to pick out corrupted pages because their type will be ODB_ELMINIT.

odb_err edba_u_lookupdeepright(edba_handle_t *handle) {
#ifdef EDB_FUCKUPS
	if(handle->clutchedentry->trashlast) {
		log_critf("trashlast must be 0");
		return ODB_ECRIT;
	}
#endif

	// easy ptrs
	edbphandle_t *edbp = handle->edbphandle;
	odb_spec_index_entry *entry = handle->clutchedentry;
	unsigned int entryid = handle->clutchedentryeid;
	int depth = entry->memory >> 0xC;
	unsigned int refmax = handle->clutchedentry->lookupsperpage;
	odb_err err;

	// (if-error-then-destroy)
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
	// To create the pages they must be given their offset, which can be
	// aquired by ref0c on the entry. But we don't want any other pages to
	// be created by other workers that would otherwise use the ref0c we just
	// used. So we have to lookuppagelock it.
	// (note to self: we lookuppagelock ref0c first sense it'll be the busier of the two)
	edbl_set(handle->lockh, EDBL_AXL, (edbl_lock){
			.type = EDBL_LREF0C,
			.eid = entryid,
	});
	// newobjectpage_offsetid - the (proposed) page offset of the first page of the strait.
	edb_pid newobjectpage_offsetid = entry->ref0c;
	// **defer: edbl_entryref0c(&handle->lockh, entryid, EDBL_UNLOCK;

	// create the new object pages
	// grab the structure
	const odb_spec_struct_struct *structdat;
	edbd_struct(handle->parent->descriptor, entry->structureid, &structdat);
	// initialize the header per edba_u_pagecreate_objects spec
	odb_spec_object header;
	header.structureid = entry->structureid;
	header.entryid = entryid;
	header.trashvor = 0;
	header.head.pleft = newobjectpage_offsetid;
	unsigned int straitc = 1 << (entry->memory & 0x000F);
	edb_pid newobjectpid;
	err = edba_u_pagecreate_objects(handle, header, structdat, straitc, &newobjectpid);
	if(err) {
		// This is an early error in here. We don't have to do too much...
		// ... nothing to roll back sense we never created any pages.
		edbl_set(handle->lockh, EDBL_ARELEASE, (edbl_lock){
				.type = EDBL_LREF0C,
				.eid = entryid,
		});
		return err;
	}
	// **defer on error: edbd_del(handle, staitc, newobjectpid);

	// atp: we have the new object pages created at newobjectpid to newobjectpid + straitc, but
	// they're entirely unreferanced (so make sure to clean them up on failure).
	// In total: we have no trashlast, we have object pages, now we need to
	// first put them in the lookup and then update trashlast.

	// From here until [[lastlookuploaded]], we have but one job: load the deepest and
	// most right lookup page in the chapter. We start by looking at the entry's
	// entry->lastlookup. But as discussed shortly, there's a chance that this lookup
	// page cannot fit any more leafs. So we must create a sibling lookup page which
	// is a complicated process.

	// Access the deep-right lookup page that is most likely to have null slots
	// open so we can reference our objects.
	//
	//  - lookupid - This is going to be working variable throughout the rest
	//    of the function. Its gunna be used a lot for multiple pageids.
	//  - lookuprefs/lookuphead - pointers to the refs and head of the currently
	//    loaded lookup page.
	//  - createdlookups[] and createdlookupsc - an array and count of lookup
	//    pages we had to end up creating to make room for our object pages.
	//    In most cases, we should have not had to create any lookup pages.
	//  - lookuppagelock - working variable used to store locking information
	//    regarding the currently loaded page (see xlloadlookup)
	//  - newreferenace - working variable. But will be the referance that
	//    was just created. Rather that be a leaf or lookup depends on its use.
	//
	// See also: rollbackcreatedpages, rollbackcreateandreference.
	edb_pid        lookuppid = entry->lastlookup;
	odb_spec_lookup_lref    *lookuprefs;
	odb_spec_lookup *lookuphead; // will be null when unloaded.
	edb_pid        createdlookups[depth];
	int            createdlookupsc = 0;
	edbl_lock  lookuppagelock;
	odb_spec_lookup_lref     newreference;
	err = xlloadlookup(handle,
	             lookuppid,
	             &lookuppagelock,
	             &lookuphead,
	             &lookuprefs);
	if(err) {
		goto rollbackcreatedpages;
	}
	// **defer: deloadlookup(handle, &lookuppagelock)

	// these variables are only needed for handling critical errors
	// in certain situations See rollbackcreateandreference label.
	//
	// In short: crit_rollback_pid will be used only in the case that
	// a lookup page "column" needs to be created. crit_rollback_pid
	// will point to the page where the new "column" has just been newly
	// referenced. And crit_rollback_refnum will be the reference id in
	// that page.
	edb_pid crit_rollback_pid;
	int crit_rollback_refnum;

	// initialize newreference with our object pages sense we have our
	// leaf-bearing lookup page loaded currently.
	newreference.ref = newobjectpid;
	// note: we use -1 here because we need the offset, not the length.
	newreference.startoff_strait = newobjectpage_offsetid + straitc-1;

	// note: '>=' because depth is 0-based
	int i;
	for(i = depth; i >= 0; i--) {

		// now we ask, is this page full?
		if (lookuphead->refc == refmax) {
			// this page is full, we must add a new lookup page to
			// newlookups.
			//
			// OR, if we're at the root lookup page and it is also full, that
			// means this entire tree is incabable of storing any more object pages.
			// ... so destroy all the lookup pages and object pages we created and
			// return ODB_ENOSPACE
			if(i == 0) {

				// the root branch page is full, this means we
				// have no more space in the lookup tree.
				log_warnf("attempted to create object pages with full OID lookup tree: entry#%d", entryid);
				err = ODB_ENOSPACE;
				deloadlookup(handle, &lookuppagelock);
				goto rollbackcreatedpages;
			}

			// this lookup page is full, but we have more branches above us.
			// So lets go up to the parent.
			// We'll also keep track of this page's last refernece so we can set the
			// pleft of the sibling page.
			edb_pid pleft = lookuppid;
			lookuppid = lookuphead->parentlookup;
			// as per spec we deload and unlock this page.
			deloadlookup(handle, &lookuppagelock);

			// (lookup-creation-assumptions)
			// At this time, with no other page loaded into our edbp handle, we
			// can create the lookup page we can /eventually/ reference in the
			// afformentioned parent.

			// Create the (sibling) lookup page
			odb_spec_lookup newheader;
			newheader.depth = i;
			newheader.entryid = entryid;

			//[[parentlookup uncertainty]]
			// You'd think that this newly created page is going to be
			// a sibling of the page we saw, so they'll have the
			// same parent....
			//
			//newheader.parentlookup = 0;
			//
			// But no. This is not a guarenteed. Because our parent can also be
			// full which would mean this new page might end up as our sibiling
			// but instead our cousin. We'll have to write the parent lookup back
			// references going back down.

			// one thing is for sure though, we know that even though the two pages
			// relationship isn't completely clear, we do know that they'll be subquentional
			// on the same generation so we can set pleft.
			newheader.head.pleft = pleft;

			// create the actual lookup
			err = edba_u_pagecreate_lookup(handle, newheader, &createdlookups[i-1], newreference);
			if(err) {
				goto rollbackcreatedpages;
			}
			createdlookupsc++;

			// sense we're about to go up the page again, we need to make sure the next
			// lookup's first ref is the lookup page we just created.
			newreference.ref = createdlookups[i-1];
			newreference.startoff_strait = newobjectpage_offsetid;

			// Now, we can loop over all the logic again with our parent.
			err = xlloadlookup(handle,
			                   lookuppid,
			                   &lookuppagelock,
			                   &lookuphead,
			                   &lookuprefs);
			if(err) {
				goto rollbackcreatedpages;
			}

			crit_rollback_refnum = lookuphead->refc;
			crit_rollback_pid    = lookuppid;
			continue;
		} else {
			// note we don't do refc-1 here because refc is the amount of /non/ null references.
			// We need the next reference right after the full ones.
			lookuprefs[lookuphead->refc] = newreference;
			lookuphead->refc++;

			// note to self: we still have the page locked and loaded
			// note to self: we always must break in this logic otherwise
			//  "i" will go to -1.
			break;
		}
	}

	// atp: we have a lookup page locked and loaded. But it may not be a leaf bearer but instead
	// a newly referenced branch (or multiple levels of branches) that need their back-references
	// made.
	// See: [[parentlookup uncertainty]]
	for(; i < depth; i++) {
		// we don't need this parent anymore.
		deloadlookup(handle, &lookuppagelock);

		// load the child we just created and install the back references.
		err = xlloadlookup(handle,
		                   newreference.ref,
		                   &lookuppagelock,
		                   &lookuphead,
		                   &lookuprefs);
		if(err) {
			goto rollbackcreateandreference;
		}

		// parent back reference.
		lookuphead->parentlookup = lookuppid;
		lookuppid = newreference.ref;

		// sense we just created this page with an exclusive lock on the lastlookup,
		// we know this page is the same way we left it: that is with a single
		// referance.
#ifdef EDB_FUCKUPS
		if(lookuphead->refc != 1) {
			log_critf("virgin page doesn't have at least 1 reference on it.");
		}
#endif
		newreference = lookuprefs[0];
	}

	// [[lastlookuploaded]]
	// atp: we have created object pages and just referenced them.
	// atp: We have a leaf-baring lookup page locked with lookuprefs[lookuphead->refc-1]
	//      pointing to our object pages.
	// atp: we have clutch-sh-lock-entry. XL trashlast, and XL ref0c. But all this
	//      function must worry about is XL ref0c.

	// we don't actually need the leaf-bearing lookup page anymore
	deloadlookup(handle, &lookuppagelock);

	// the final indicator that we've completed making new object pages and referenced
	// is to update ref0c and unlock it so other workers may create pages.
	entry->lastlookup = lookuppid;
	entry->ref0c += straitc;
	telemetry_pages_newobj(entryid, newobjectpid, straitc);
	edbl_set(handle->lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LREF0C,
			.eid = entryid,
	});

	// and then let the trash collection know these pages are now in circulation.
	// we -1 here to convert created count to page id, otherwise we'd be
	// pointing to a page that doesn't exist yet.
	entry->trashlast = newobjectpid + straitc-1;

	// We're done here.
	return 0;

	// everything past here are error escapes

	rollbackcreateandreference:

	// this is a tricky situiation. What has happened here is we created all the
	// lookups, objects, and then referenced all them back to the root. But we
	// have now ran into an error during back-referencing, so we need to unreferece
	// the stack of pages we just referenced.
	//
	// This is (hopefully) a very rare occurrence. So the console we're free to light
	// up the logs.
	log_critf("need to dereference pages due to critical error. Prepare for error cascades!");

	err = xlloadlookup(handle,
	                   crit_rollback_pid,
	                   &lookuppagelock,
	                   &lookuphead,
	                   &lookuprefs);
	if(err) {
		log_critf("failed to dereferance rouge lookuppage %ld from its parent in entry %d.\n"
				  "This will cause this entire chapter to behave unpredictably.",
				  crit_rollback_pid, entryid);
	} else {
		lookuprefs[crit_rollback_refnum].ref = 0;
		edbp_mod(edbp, EDBP_CACHEHINT, EDBP_HDIRTY);
		deloadlookup(handle, &lookuppagelock);
	}

	// note: no deloadlookup already called.
	rollbackcreatedpages:

	// unlock entryref0c sense we know none of these pages are referenced.
	// thus we can allow other workers to go in there and try to do the same function.
	edbl_set(handle->lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LREF0C,
			.eid = entryid,
	});

	// delete the lookup pages
	for(int d = 0; d < createdlookupsc; d++) {
		if (edbd_del(handle->parent->descriptor, 1, createdlookups[d])) {
			log_critf("page leak: pid %ld", createdlookups[d]);
		}
	}

	// delete the object pages
	// roll back the object pages
	if (edbd_del(handle->parent->descriptor, straitc, newobjectpid)) {
		log_critf("page leak: pid %ld-%ld", newobjectpid, newobjectpid + straitc);
	}

	return err;
}

odb_err edba_u_pagecreate_lookup(edba_handle_t *handle,
                                 odb_spec_lookup header,
                                 edb_pid *o_pid,
                                 odb_spec_lookup_lref ref) {

	// easy ptrs
	edbphandle_t *edbp = handle->edbphandle;
	edbd_t *descriptor = handle->parent->descriptor;
	odb_err err;

	// later: need to reuse deleted pages rather than creating them by utilizing the
	//        deleted page / trash line

	// create the page
	err = edbd_add(descriptor, 1, o_pid);
	if(err) {
		return err;
	}

	// start the page and 0-initialize it
	err = edbp_start(edbp, *o_pid);
	if(err) {
		log_debugf("failed to start page after it's creation, "
				   "to prevent if from going unrefanced, will be attempting to delete it.");
		if(edbd_del(descriptor, 1, *o_pid)) {
			log_critf("page leak! pid %ld is unreferanced", *o_pid);
		}
		return err;
	}
	// **defer: edbp_finish(edbp);
	void *page = edbp_graw(edbp);
	bzero(page, edbd_size(descriptor));
	odb_spec_lookup *pageheader = (odb_spec_lookup *)page;
	odb_spec_lookup_lref *pagerefs = page + ODB_SPEC_HEADSIZE;

	// write the header
	pageheader->entryid = header.entryid;
	if(ref.ref == 0) {
		pageheader->refc = 0;
	} else {
		pageheader->refc = 1;
	}
	pageheader->parentlookup = header.parentlookup;
	pageheader->depth = header.depth;
	pageheader->head.ptype = ODB_ELMLOOKUP;
	pageheader->head.pleft = header.head.pleft;
	pageheader->head.pright = header.head.pright;

	// first header
	pagerefs[0] = ref;

	// note: don't need to write the header, we can leave it as all 0s.

	// set some cache hints
	// we'll set use soon because its almost impossible to create a lookup page
	// without needing to write something in it.
	edbp_mod(edbp, EDBP_CACHEHINT, EDBP_HDIRTY
	                               | EDBP_HUSESOON| (EDBP_HINDEX3 * (4-header.depth)));
	edbp_finish(edbp);

	return 0;
}

odb_err edba_u_pagecreate_objects(edba_handle_t *handle,
                                  odb_spec_object header,
                                  const odb_spec_struct_struct *strct,
                                  uint8_t straitc, edb_pid *o_pid) {
	// easy ptrs
	edbphandle_t *edbp = handle->edbphandle;
	odb_err err;
	edbd_t *descriptor = handle->parent->descriptor;
	unsigned int objectsperpage = (edbd_size(handle->parent->descriptor) -
			ODB_SPEC_HEADSIZE)
			/ strct->fixedc;

	// create the pages
	err = edbd_add(descriptor, straitc, o_pid);
	if(err) {
		return err;
	}

	// now we must loop through each page and initalize it.
	for(int i = 0; i < straitc; i++) {
		// start the page
		err = edbp_start(edbp, *o_pid+i);
		if (err) {
			log_debugf("failed to start page after it's creation, "
			           "to prevent if from going unrefanced, will be attempting to delete it.");
			if (edbd_del(handle->parent->descriptor, 1, *o_pid)) {
				log_critf("page leak! pid %ld is unreferanced", *o_pid);
			}
			return err;
		}
		// **defer: edbp_finish(edbp);

		// initiate the page
		void *page = edbp_graw(edbp);
		bzero(page, edbd_size(descriptor));
		edba_u_initobj_pages(page, header, strct->fixedc, objectsperpage);

		// we do the referance  logic after so the first iteration of
		// initobjectspage uses the trashvor/pleaft supplied by the caller of this
		// function.
		header.trashvor = *o_pid + i;
		header.head.pleft++;

		// set hints and close
		edbp_mod(edbp, EDBP_CACHEHINT, EDBP_HDIRTY);
		edbp_finish(edbp);
	}

	return 0;
}
