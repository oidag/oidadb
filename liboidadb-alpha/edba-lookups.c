#include "edba.h"
#include <oidadb-internal/odbfile.h>

// converts a pid offset to the actual page address
// note to self: the only error returned by this should be a critical error
//
// assumptions:
//     pidoffset_search is less than the total amount of pages in the edbp_object chapter
odb_err static edba_u_lookup_rec(edba_handle_t *handle,
                                 odb_pid lookuproot,
                                 odb_pid selfpagestartoffset,
                                 odb_pid chapter_pageoff,
                                 odb_pid *o_pid,
                                 int depth) {
	// install sh lock on first byte of page per Object-Reading spec
	edbl_lock lock = {
			.type = EDBL_LLOOKUP_EXISTING,
			.lookup_pid = lookuproot,
	};

	// ** defer: edbl_set(&self->lockdir, lock);
	edbl_set(handle->lockh, EDBL_ASH, lock);

	// ** defer: edbp_finish(&self->edbphandle);
	odb_err err = edbp_start(handle->edbphandle, lookuproot);
	if(err) {
		edbl_set(handle->lockh, EDBL_ARELEASE, lock);
		return err;
	}
	// set the lookup hint now
	// generate the proper EDBP_HINDEX... value
	edbp_hint h = EDBP_HINDEX0 - depth * 0x10;
	edbp_mod(handle->edbphandle, EDBP_CACHEHINT, h);
	odb_spec_lookup *l = edbp_graw(handle->edbphandle);
	odb_spec_lookup_lref *refs = (void*)l + ODB_SPEC_HEADSIZE;
#ifdef EDB_FUCKUPS
	if(l->refc == 0) {
		log_critf("edba_u_lookupoid was called and referenced lookup node has "
		          "no referenced");
	}
#endif

	if(depth == 0) {
		// if depth is 0 that means this page is full of leaf node references.
		// more specifically, full of leaf node /page strait/ references.
		// We know that pidoffset_search is somewhere in one of these straits.
		int i;
		for(i = 0; i < l->refc; i++) {
			// it is now the END offset
			if(refs[i].startoff_strait >= chapter_pageoff) {
				// Our offset is in this reference sense its the last page id
				// in this reference's strait is equal or larger than the
				// page we're looking for.
				break;
			}
			// when calling this function, selfpagestartoffset is set to
			// equal the starting page offset of the first reference. But
			// sense we have moved passed that reference, then we set it
			// equal to refs[i]'s last recorded page offset +1: which is
			// equal to the starting offset of the next reference.
			selfpagestartoffset = refs[i].startoff_strait+1;
		}
		// we know that this reference contains our page in its strait.
		// So if our offset search is lets say 5, and this strait contained
		// pageoffsets 4,5,6,7,8 and associating pageids of 42,43,44,45,46.
		// In this example, we need to calculate page id 43 to get our offset
		// page of 5.
		//
		// Without knowing what the starting offset is (ie 4) in our strait,
		// we'd be screwed to know how far we must seek through this strait.
		// This is why we have selfpagestartoffset.
		//
		// So we just need to do the following:
		//   - refs[i].ref + (chapter_pageoff - selfpagestartoffset);
		//   - ie: 42 + (5 - 4) (= 43)
		//
		// also note that refc will always be non-null references. So ref[i]
		// will never be a null reference.
		*o_pid = refs[i].ref + (chapter_pageoff - selfpagestartoffset);

		// So lets finish out of this page...
		edbp_finish(handle->edbphandle);
		// and release the lock
		edbl_set(handle->lockh, EDBL_ARELEASE, lock);
		return 0;
	}

	// atp: we are not at the deepest page, so we have to travel further down
	//      the b-tree

	// search in the list of references of this page to find out where we
	// need to go next.
	int i;
	for(i = 0; i < l->refc; i++) {
		if(i+1 == l->refc || refs[i+1].startoff_strait > chapter_pageoff) {
			// logically, if we're in here that means the next interation (i+1)
			// will be the end ouf our reference list, or, will be a reference
			// that has a starting offset that is larger than this current
			// iteration. Thus, we can logically deduce that our offset is somewhere
			// down in this iteration.
			//
			// To clear out some scope, lets break out and continue
			break;
		}
	}
	// note: based on our logic in the if statement, i will never equal l->refc.
#ifdef EDB_FUCKUPS
	if(i == l->refc) {
		log_critf("failed to find child lookup page under the "
				  "logical assumption that it exists.");
	}
#endif

	// at this point, we know that refs[i] is the reference we must follow.
	// Lets throw the important number in our stack.
	lookuproot = refs[i].ref;
	selfpagestartoffset = refs[i].startoff_strait;

	// So lets finish out of this page...
	edbp_finish(handle->edbphandle);
	// and release the lock
	edbl_set(handle->lockh, EDBL_ARELEASE, lock);

	// now we can recurse down to the next lookup page.
	return edba_u_lookup_rec(handle,
							 lookuproot,
							 selfpagestartoffset,
							 chapter_pageoff,
							 o_pid, depth-1);
}

odb_err edba_u_lookupoid(edba_handle_t *handle, odb_spec_index_entry *entry,
                         odb_pid chapter_pageoff, odb_pid *o_pid) {
	if(chapter_pageoff >= entry->ref0c) {
		return ODB_EEOF;
	}
	odb_err err = edba_u_lookup_rec(handle, entry->ref1, 0, chapter_pageoff,
	                                o_pid, entry->memory >> 12);
#ifdef EDB_FUCKUPS
	if(*o_pid == 0) {
		log_critf("o_pid returned 0 from lookup despite it being in page "
				  "range");
	}
#endif
	return err;
}
