#include "include/ellemdb.h"
#include "edba.h"
#include "edba-util.h"

edb_err edba_entryopenc(edba_handle_t *h, edb_eid *o_eid, edbf_flags flags) {

#ifdef EDB_FUCKUPS
	if(h->clutchedentry) {
		log_critf("call to edba_entryopenc when entry already clutched!");
	}
#endif

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

void    edba_entryclose(edba_handle_t *h) {
	edba_u_clutchentry_release(h);
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