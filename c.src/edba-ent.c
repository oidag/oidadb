#include "include/ellemdb.h"
#include "edba.h"
#include "edba-util.h"

edb_err edba_entryopenc(edba_handle_t *h, edb_eid *o_eid, edbf_flags flags) {

#ifdef EDB_FUCKUPS
	if(h->clutchedentry) {
		log_critf("call to edba_entryopenc when an entry already clutched!");
		return EDB_ECRIT;
	}
	if(!(flags & EDBA_FCREATE)) {
		log_critf("flags must include FCREATE for edba_entryopenc");
		return EDB_ECRIT;
	}
#endif

	// handle-status politics
	if(h->opened != 0) {
		log_critf("cannot open entry, something already opened");
		return EDB_ECRIT;
	}
	h->opened = EDB_TENTS;
	h->openflags = flags;

	// easy pointers
	edbd_t *descriptor = h->parent->descriptor;
	edbl_handle_t *lockh = &h->lockh;
	edb_err err;

	// as per spec, lock the mutex and obtain a XL clutch lock
	// **defer: edbl_entrycreaiton_release
	edbl_entrycreaiton_lock(lockh);

	// note the absence of edba_u_clutchentry. We manually clutch it here because
	// we need to surf through the index.
	// find the first EDB_TINIT
	for (h->clutchedentryeid = 0; !err && h->clutchedentry->type != EDB_TINIT; h->clutchedentryeid++) {
		err = edbd_index(descriptor, h->clutchedentryeid, &h->clutchedentry);
	}
	if(err) {
		edbl_entrycreaiton_release(lockh);
		if(err == EDB_EEOF) {
			// well this sucks. No more entries availabe in our database. that sucks
			log_warnf("index is maxed out");
			return EDB_ENOSPACE;
		}
		log_critf("unknown warning when surfing index: %d", err);
		return EDB_ECRIT;
	}
	*o_eid = h->clutchedentryeid;

	// at this point we know that h->clutchedentry and o_eid is pointing to valid
	// EDB_TINIT and we are inside the creation mutex.
	// As per spec, now we get an XL mutex.
	edbl_entry(&h->lockh, h->clutchedentryeid, EDBL_EXCLUSIVE);
	h->clutchedentry->type = EDB_TPEND;
	// as per spec, release the creaiton mutex
	edbl_entrycreaiton_release(lockh);

	// Now we leave this function with only the XL clutch lock on the entry to be
	// released in the closed statement
	return 0;
}

edb_err edba_entryset(edba_handle_t *h, edb_entry_t e) {
#ifdef EDB_FUCKUPS
	if(!(h->openflags & EDBA_FWRITE) || h->opened != EDB_TENTS) {
		log_critf("edba_entryset: no FWRITE on EDB_TENTS");
		return EDB_ECRIT;
	}
#endif

	// easy pointers
	edb_entry_t *entry = h->clutchedentry;
	edbd_t *descriptor = h->parent->descriptor;
	edbphandle_t *edbphandle = &h->edbphandle;
	const edb_struct_t *strck;
	edb_err err;

	// value assumptions
	// clear out all bits that are not used
	e.memory = e.memory & 0x3f0f;
	int depth = e.memory >> 0xC;

	// validation
	if(h->openflags & EDBA_FCREATE) {
		switch (e.type) {
			case EDB_TOBJ:
				break;
			default: return EDB_EINVAL;
		}
	}
	err = edbd_struct(descriptor, e.structureid, &strck);
	if(err) {
		// EDB_EEOF
		return err;
	}

	// atp (at this point): entry imput parameters is good. Time to get some
	// work done.

	if(!(h->openflags & EDBA_FWRITE)) {
		// they're updating, lets put a implement here.
		// todo: implement structure updating
		implementme();
		return EDB_ECRIT;
	}

	// atp: the structure is valid and they're trying to create a new entry
	// with valid parameters. We know that entry is initialized with 0's save
	// for the type which still be EDB_TPEND.
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
	edb_pid parentid = 0;
	edb_pid lookuppages[depth+1];
	for (int i = 0; i <= depth; i ++) { // "<=" because depth is 0-based.
		edbp_lookup_t lookup_header;
		lookup_header.entryid = h->clutchedentryeid;
		lookup_header.parentlookup = parentid;
		lookup_header.depth = i;
		lookup_header.head.pleft = 0;
		lookup_header.head.pright = 0;
		err = edba_u_pagecreate_lookup(h, lookup_header, &lookuppages[i], (edb_lref_t){0});
		if(err) {
			// failed for whatever reason,
			// roll back page creations
			i--; // roll back to the last itoration where we did successfully create a page.
			for( ; i >= 0; i--) {
				if(edbd_del(descriptor, 1, lookuppages[i])) {
					log_critf("page leak: %ld", lookuppages[i]);
				}
			}
			return err;
		}
		if(i == 0) {
			// this is the root page so it's ref1.
			e.ref1 = lookuppages[i];
		}
		parentid = lookuppages[i];
	}
	// parentid will be set to the deepest lookup page, and sense
	// this is our only branch, this is by definition the deep-right.
	e.lastlookup = parentid;

	// remaining red-tape
	// 0 out the reserved block just for future refeance.
	e.rsvd = 0;
	//e.type = EDB_TOBJ; (just to make corruptiong VERY obvious, we'll save this after)
	e.lookupsperpage = (edbd_size(edbphandle->parent->fd) - EDBD_HEADSIZE) / sizeof(edb_lref_t);
	e.objectsperpage = (edbd_size(edbphandle->parent->fd) - EDBD_HEADSIZE) / strck->fixedc;
	e.trashlast = 0;

	// we're all done, save to persistant memory.
	*entry = e;
	entry->type = EDB_TOBJ; // the final "we're done" marker.
	return 0;
}

const edb_entry_t *edba_entrydatr(edba_handle_t *h) {
	return h->clutchedentry;
}

void    edba_entryclose(edba_handle_t *h) {
#ifdef EDB_FUCKUPS
	if(h->opened!= EDB_TENTS) {
		log_debugf("trying to close entry when non opened.");
	}
#endif
	edba_u_clutchentry_release(h);
	h->opened = 0;
}

edb_err edba_entrydelete(edba_handle_t *h, edb_eid eid) {

	// handle-status politics
	if(h->opened != 0) {
		log_critf("cannot open entry, something already opened");
		return EDB_ECRIT;
	}
	h->opened = EDB_TENTS;

	// easy pointers
	edbd_t *descriptor = h->parent->descriptor;
	edbl_handle_t *lockh = &h->lockh;
	edb_err err;

	edba_u_clutchentry(h, eid, 1);
	//**defer: edba_u_clutchentry_release

	// set it to EDB_TPEND so we can more easily sniff out corrupted
	// operations if we crash mid-delete.
	h->clutchedentry->type = EDB_TPEND;

	// todo: delete everythign

	edbl_entrycreaiton_lock(lockh);
	h->clutchedentry->type  = EDB_TINIT;
	edba_u_clutchentry_release(h);
	edbl_entrycreaiton_release(lockh);
	return 0;
}

edb_err edba_u_clutchentry(edba_handle_t *handle, edb_eid eid, int xl) {
#ifdef EDB_FUCKUPS
	if(handle->clutchedentry) {
		log_critf("attempting to clutch an entry with handle already clutching something. Or perhaps unitialized handle");
	}
#endif
	// SH lock the entry
	if(xl) {
		edbl_entry(&handle->lockh, eid, EDBL_EXCLUSIVE);
	} else {
		edbl_entry(&handle->lockh, eid, EDBL_TYPSHARED);
	}
	edb_err err = edbd_index(handle->parent->descriptor, eid, &handle->clutchedentry);
	if(err) {
		edbl_entry(&handle->lockh, eid, EDBL_TYPUNLOCK);
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
	edbl_entry(&handle->lockh, handle->clutchedentryeid, EDBL_TYPUNLOCK);
	handle->clutchedentry = 0;
	handle->clutchedentryeid = 0;
}