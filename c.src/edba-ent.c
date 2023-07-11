#include "edba.h"
#include "edba_u.h"
#include "include/oidadb.h"

odb_err edba_entryopenc(edba_handle_t *h, odb_eid *o_eid, edbf_flags flags) {

#ifdef EDB_FUCKUPS
	if(h->clutchedentry) {
		log_critf("call to edba_entryopenc when an entry already clutched!");
		return ODB_ECRIT;
	}
	if(!(flags & EDBA_FCREATE)) {
		log_critf("flags must include FCREATE for edba_entryopenc");
		return ODB_ECRIT;
	}
#endif

	// handle-status politics
	if(h->opened != 0) {
		log_critf("cannot open entry, something already opened");
		return ODB_ECRIT;
	}
	h->opened = ODB_ELMENTS;
	h->openflags = flags;

	// easy pointers
	edbd_t *descriptor = h->parent->descriptor;
	edbl_handle_t *lockh = h->lockh;
	odb_err err = 0;

	// as per spec, lock the mutex and obtain a XL clutch lock
	// **defer: edbl_entrycreaiton_release
	edbl_set(lockh, EDBL_AXL, (edbl_lock){
			.type = EDBL_LENTCREAT,
	});

	// note the absence of edba_u_clutchentry. We manually clutch it here because
	// we need to surf through the index.
	// find the first ODB_ELMINIT
	for (h->clutchedentryeid = EDBD_EIDSTART; !err; h->clutchedentryeid++) {
		err = edbd_index(descriptor, h->clutchedentryeid, &h->clutchedentry);
		if(h->clutchedentry->type == ODB_ELMINIT
				|| h->clutchedentry->type == ODB_ELMDEL) {
			break;
		}
	}
	if(err) {
		edbl_set(lockh, EDBL_ARELEASE, (edbl_lock){
				.type = EDBL_LENTCREAT,
		});
		if(err == ODB_EEOF) {
			// well this sucks. No more entries availabe in our database. that sucks
			log_warnf("index is maxed out");
			return ODB_ENOSPACE;
		}
		log_critf("unknown warning when surfing index: %d", err);
		return ODB_ECRIT;
	}
	*o_eid = h->clutchedentryeid;

	// at this point we know that h->clutchedentry and o_eid is pointing to valid
	// ODB_ELMINIT and we are inside the creation mutex.
	// As per spec, now we get an XL mutex.
	edbl_set(h->lockh, EDBL_AXL, (edbl_lock){
		.type = EDBL_LENTRY,
		.eid = h->clutchedentryeid,
	});
	h->clutchedentry->type = ODB_ELMPEND;
	// as per spec, release the creaiton mutex
	edbl_set(lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LENTCREAT,
	});

	// Now we leave this function with only the XL clutch lock on the entry to be
	// released in the closed statement
	return 0;
}

odb_err edba_entryset(edba_handle_t *h, odb_spec_index_entry e) {
#ifdef EDB_FUCKUPS
	if(!(h->openflags & EDBA_FWRITE) || h->opened != ODB_ELMENTS) {
		log_critf("edba_entryset: no FWRITE on ODB_ELMENTS");
		return ODB_ECRIT;
	}
#endif

	// easy pointers
	odb_spec_index_entry *entry = h->clutchedentry;
	edbd_t *descriptor = h->parent->descriptor;
	edbphandle_t *edbphandle = h->edbphandle;
	const odb_spec_struct_struct *strck;
	odb_err err;

	// value assumptions
	// clear out all bits that are not used
	e.memory = e.memory & 0x3f0f;
	int depth = e.memory >> 0xC;

	// validation
	if(h->openflags & EDBA_FCREATE) {
		switch (e.type) {
			case ODB_ELMOBJ:
				break;
			default: return ODB_EINVAL;
		}
	}
	err = edbd_struct(descriptor, e.structureid, &strck);
	if(err) {
		// ODB_EEOF
		return err;
	}

	// atp (at this point): entry imput parameters is good. Time to get some
	// work done.

	if(!(h->openflags & EDBA_FWRITE)) {
		// they're updating, lets put a implement here.
		// todo: implement structure updating
		implementme();
		return ODB_ECRIT;
	}

	// atp: the structure is valid and they're trying to create a new entry
	// with valid parameters. We know that entry is initialized with 0's save
	// for the type which still be ODB_ELMPEND.
	// but we can copy over the memory and structure settings

	// to make corruption easy to detect: we set *entry = e; at the very last.

	// ref0: objects page(s)
	e.ref0 = 0;
	e.ref0c = 0;

	// ref2: dynamic page(s)
	// these are created as we go.
	e.ref2c = 0;
	e.ref2 = 0;

	// ref1: lookup-oid page root
	// We must create a full stack of OID lookup pages so we can
	// fill out lastlookup, last lookup must point to a page of max depth.
	odb_pid parentid = 0;
	odb_pid lookuppages[depth + 1];
	// our for-loop, we go backwards, creating the depthest page first so
	// that we can reference that child page to its parent in the subsequent
	// itoration.
	odb_spec_lookup_lref childref = {0};
	for (int i = depth; i >= 0; i--) { // ">=" because depth is 0-based.
		odb_spec_lookup lookup_header;
		lookup_header.entryid = h->clutchedentryeid;
		//lookup_header.parentlookup = ???; // see note after loop
		lookup_header.depth = i;
		lookup_header.head.pleft = 0;
		lookup_header.head.pright = 0;
		err = edba_u_pagecreate_lookup(h, lookup_header, &lookuppages[i], childref);
		if(err) {
			// failed for whatever reason,
			// roll back page creations
			i++; // roll back to the last itoration where we did successfully
			// create a page.
			for( ; i <= depth; i++) {
				if(edbd_del(descriptor, 1, lookuppages[i])) {
					log_critf("page leak: %ld", lookuppages[i]);
				}
			}
			return err;
		}
		if(i == depth) {
			// this is our currently deepest rightest lookup page.
			e.lastlookup = lookuppages[i];
		}
		if(i == 0) {
			// this is the root page so it's ref1.
			e.ref1 = lookuppages[i];
		}

		// build the child reference for our parent page.
		childref.ref = lookuppages[i];
		childref.startoff_strait = 0;
	}

	// one last step. The children do not have their
	// header->parentlookup set. Lets go through now and set this back
	// reference.
	//
	// We could have done this a more
	// efficient loop but this function is rarely called. so we can be a bit
	// lazy.
	for (int i = 0; i <= depth; i ++) {
		err = edbp_start(edbphandle, lookuppages[i]);
		if(err) {
			// bail out
			for( i=0 ; i <= depth; i++) {
				if(edbd_del(descriptor, 1, lookuppages[i])) {
					log_critf("page leak: %ld", lookuppages[i]);
				}
			}
			return err;
		}
		odb_spec_lookup *page = edbp_graw(edbphandle);
		if(i == 0) {
			// root page, no parent.
			page->parentlookup = 0;
		} else {
			page->parentlookup = lookuppages[i-1];
		}

		edbp_finish(edbphandle);
	}


	// remaining red-tape
	// 0 out the reserved block just for future refeance.
	e.rsvd = 0;
	//e.type = ODB_ELMOBJ; (just to make corruptiong VERY obvious, we'll save this after)
	e.lookupsperpage = (edbd_size(h->parent->descriptor) - ODB_SPEC_HEADSIZE) / sizeof(odb_spec_lookup_lref);
	e.objectsperpage = (edbd_size(h->parent->descriptor) - ODB_SPEC_HEADSIZE) /
			strck->fixedc;
	e.trashlast = 0;

	// we're all done, save to persistant memory.
	*entry = e;
	entry->type = ODB_ELMOBJ; // the final "we're done" marker.
	return 0;
}

const odb_spec_index_entry *edba_entrydatr(edba_handle_t *h) {
	return h->clutchedentry;
}

void    edba_entryclose(edba_handle_t *h) {
#ifdef EDB_FUCKUPS
	if(h->opened != ODB_ELMENTS) {
		log_debugf("trying to close entry when non opened.");
	}
#endif
	edba_u_clutchentry_release(h);
	h->opened = 0;
}

odb_err edba_entrydelete(edba_handle_t *h, odb_eid eid) {

	// handle-status politics
	if(h->opened != 0) {
		log_critf("cannot open entry, something already opened");
		return ODB_ECRIT;
	}
	h->opened = ODB_ELMENTS;

	// easy pointers
	edbd_t *descriptor = h->parent->descriptor;
	edbl_handle_t *lockh = h->lockh;
	odb_err err;

	edba_u_clutchentry(h, eid, 1);
	//**defer: edba_u_clutchentry_release

	// set it to ODB_ELMPEND so we can more easily sniff out corrupted
	// operations if we crash mid-delete.
	h->clutchedentry->type = ODB_ELMPEND;

	// todo: delete everythign
	implementme();

	edbl_set(lockh, EDBL_AXL, (edbl_lock){
		.type = EDBL_LENTCREAT,
	});
	h->clutchedentry->type  = ODB_ELMDEL;
	edba_u_clutchentry_release(h);
	edbl_set(lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LENTCREAT,
	});
	return 0;
}

odb_err edba_u_clutchentry(edba_handle_t *handle, odb_eid eid, int xl) {
#ifdef EDB_FUCKUPS
	if(handle->clutchedentry) {
		log_critf("attempting to clutch an entry with handle already clutching something. Or perhaps unitialized handle");
	}
#endif
	// SH lock the entry
	edbl_act act = EDBL_ASH;
	if(xl) {
		act = EDBL_AXL;
	}
	edbl_set(handle->lockh, act, (edbl_lock){
		.type = EDBL_LENTRY,
		.eid = eid,
	});
	odb_err err = edbd_index(handle->parent->descriptor, eid, &handle->clutchedentry);
	if(err) {
		edbl_set(handle->lockh, EDBL_ARELEASE, (edbl_lock){
				.type = EDBL_LENTRY,
				.eid = eid,
		});
		return err;
	}
	handle->clutchedentryeid = eid;
	return 0;
}
void edba_u_clutchentry_release(edba_handle_t *handle) {
#ifdef EDB_FUCKUPS
	if(!handle->clutchedentry) {
		log_critf("attempting to realse a clutch entry when nothing is there.");
	}
#endif
	edbl_set(handle->lockh, EDBL_ARELEASE, (edbl_lock){
			.type = EDBL_LENTRY,
			.eid = handle->clutchedentryeid,
	});
	handle->clutchedentry = 0;
	handle->clutchedentryeid = 0;
}

odb_err edba_entity_get(edba_handle_t *h
		, uint32_t *o_entc
		, struct odb_entstat *o_ents) {

#ifdef EDB_FUCKUPS
	if(h->clutchedentry) {
		log_critf("edba_entity_get cannot be called with entry already "
				  "cluteched.");
	}
#endif

	// easy inits
	odb_err err = 0;
	odb_eid eid;
	odb_spec_index_entry *entry;
	edbl_lock lock;
	struct odb_entstat entstat;
	uint32_t o_entstat_capacity; // used to track our capacity of o_ents
	if(o_ents) {
		o_entstat_capacity = *o_entc;
	}
	*o_entc = 0;

	for(eid = EDBD_EIDSTART; ; eid++) {
		// SH lock for EDBL_LENTRY.
		lock.type = EDBL_LENTRY;
		lock.eid = eid;
		edbl_set(h->lockh, EDBL_ASH, lock);
		// **defer: edbl_set(h->lockh, EDBL_ARELEASE, lock);
		err = edbd_index(h->parent->descriptor, eid, &entry);
		if(err || entry->type == ODB_ELMINIT) {
			if(err == ODB_EEOF) {
				// all good. This just means we've read the last entity of the
				// index.
				err = 0;
			} else if (err) {
				err = log_critf("unhandled error: %d", err);
			}
			// end of all the entities that are useful to us.
			edbl_set(h->lockh, EDBL_ARELEASE, lock);
			return err;
		}
		if(entry->type != ODB_ELMOBJ) {
			// not an object, skip.
			edbl_set(h->lockh, EDBL_ARELEASE, lock);
		}

		// we know now that this is a valid structure we'd want to return.

		*o_entc = *o_entc+1;
		if(o_ents) {
			entstat.type = entry->type;
			entstat.memorysettings = entry->memory;
			entstat.structureid = entry->structureid;
			entstat.pagec = entry->ref0c;
			o_ents[*o_entc-1] = entstat; // -1 sense entc is the count,
			if(*o_entc == o_entstat_capacity) {
				// our o_ents array has run out of capacity, we cannot put in
				// anymore structures, so return.
				edbl_set(h->lockh, EDBL_ARELEASE, lock);
				return 0;
			}
		}


		// release lock to prepare us for the next one.
		edbl_set(h->lockh, EDBL_ARELEASE, lock);
	}

	// (note the above loop will always return)
}