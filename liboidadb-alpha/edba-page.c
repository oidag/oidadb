#include "edba.h"
#include "edba_u.h"
#include <oidadb/oidadb.h>

static void assignpage(edba_handle_t *h, odb_pid pid, void *page) {
	h->pagehead = page;
	h->pagepid  = pid;
	h->pagebody = page + ODB_SPEC_HEADSIZE;
}

odb_err edba_pageopen(edba_handle_t *h, odb_eid eid, odb_pid offset,
                      edbf_flags flags) {
	odb_err err;

	// handle-status politics.
	if(h->opened != 0) {
		log_critf("cannot open page, something already opened");
		return ODB_ECRIT;
	}
	h->openflags = flags;

	// cluth lock the entry
	err = edba_u_clutchentry(h, eid, 0);
	if(err) {
		return err;
	}

	// get the struct data
	const odb_spec_struct_struct *structdat;
	edbd_struct(h->parent->descriptor, h->clutchedentry->structureid, &structdat);

	// do the oid lookup to get the page.
	odb_pid foundpid;
	err = edba_u_lookupoid(h, h->clutchedentry, offset, &foundpid);
	if(err) {
		edba_u_clutchentry_release(h);
		return err;
	}

	// aquire the lock on the page body
	h->lock = (edbl_lock) {
			.type = EDBL_LOBJBODY,
			.object_pid = foundpid,
			.page_size  = edbd_size(h->parent->descriptor),
	};
	if(flags & EDBA_FWRITE) {
		edbl_set(h->lockh, EDBL_AXL, h->lock);
	} else {
		edbl_set(h->lockh, EDBL_ASH, h->lock);
	}
	// **defer: edbl_set(h->lockh, EDBL_ARELEASE, h->lock);

	// load the page into cache.
	err = edbp_start(h->edbphandle, foundpid);
	if(err) {
		edbl_set(h->lockh, EDBL_ARELEASE, h->lock);
		edba_u_clutchentry_release(h);
		return err;
	}

	// set the pointer, fill out the handle's pointers
	assignpage(h, foundpid, edbp_graw(h->edbphandle));
	h->opened = ODB_ELMOBJPAGE;
	return 0;
}

odb_err edba_pageclose(edba_handle_t *h) {
	assert(h->opened == ODB_ELMOBJPAGE);
	if(h->pagehead) {
		edbp_finish(h->edbphandle);
	}
	edbl_set(h->lockh, EDBL_ARELEASE, h->lock);
	edba_u_clutchentry_release(h);
	h->opened = 0;
}

odb_err edba_pageadvance(edba_handle_t *h) {
	assert(h->opened == ODB_ELMOBJPAGE);
	odb_err err;

	// note: see locking.org

	// Read the =pright=
	edbl_lock lock = {
			.type = EDBL_LOBJPRIGHT,
			.object_pid = h->pagepid,
	};
	edbl_set(h->lockh, EDBL_ASH, lock);
	odb_pid newpid = h->pagehead->head.pright;

	// Is there a page after this?
	if(newpid == 0) {
		// no next page. In this case we haven't deloaded anything, its as if
		// this function was never called (apart from now having an ODB_EEOF) to deal with.
		edbl_set(h->lockh, EDBL_ARELEASE, lock);
		return ODB_EEOF;
	}

	// there is another page after this, lets release the lock from this current page.
	// We can also deload it sense we don't need any more info from it.
	edbl_set(h->lockh, EDBL_ARELEASE, h->lock);
	edbp_finish(h->edbphandle);

	// our h->lock is already set up with a EDBL_LOBJBODY, we just need to update the pid.
	h->lock.object_pid = newpid;
	if(h->openflags & EDBA_FWRITE) {
		edbl_set(h->lockh, EDBL_AXL, h->lock);
	} else {
		edbl_set(h->lockh, EDBL_ASH, h->lock);
	}

	// finally, load the page.
	if((err = edbp_start(h->edbphandle, newpid))) {
		h->pagehead = 0; // let edba_pageclose we don't have a page loaded.
		return err;
	}
	assignpage(h, newpid, edbp_graw(h->edbphandle));

	return 0;

}

unsigned int edba_pageobjectv_count(edba_handle_t *h) {
	assert(h->opened == ODB_ELMOBJPAGE);
	return h->clutchedentry->objectsperpage;
}

unsigned int edba_pageobjectc(edba_handle_t *h) {
	assert(h->opened == ODB_ELMOBJPAGE);
	return edbd_size(h->parent->descriptor) - ODB_SPEC_HEADSIZE;
}

const void *edba_pageobjectv_get(edba_handle_t *h) {
	assert(h->opened == ODB_ELMOBJPAGE);
	if ((h->openflags & EDBA_FREAD) != EDBA_FREAD) {
		log_critf("no read permissions");
		return 0;
	}
	return h->pagebody;
}

void *edba_pageobjectv(edba_handle_t *h) {
	assert(h->opened == ODB_ELMOBJPAGE);
	if (!(h->openflags & EDBA_FWRITE)) {
		log_critf("no write permissions");
		return 0;
	}
	return h->pagebody;
}



const odb_spec_struct_struct *edba_pagestruct(edba_handle_t *h) {
	assert(h->opened == ODB_ELMOBJPAGE);
	const odb_spec_struct_struct *ret;
	edbd_struct(h->parent->descriptor, edba_pagestructid(h), &ret);
	return ret;
}

odb_sid edba_pagestructid(edba_handle_t *h) {
	assert(h->opened == ODB_ELMOBJPAGE);
	return h->clutchedentry->structureid;
}