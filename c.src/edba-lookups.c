#include "edba.h"
#include "odb-structures.h"

// converts a pid offset to the actual page address
// note to self: the only error returned by this should be a critical error
//
// assumptions:
//     pidoffset_search is less than the total amount of pages in the edbp_object chapter
edb_err static edba_u_lookup_rec(edba_handle_t *handle, edb_pid lookuproot,
                          edb_pid chapter_pageoff, edb_pid *o_pid, int depth) {
	// install sh lock on first byte of page per Object-Reading spec
	edbl_lock lock = {
			.type = EDBL_LLOOKUP_EXISTING,
			.lookup_pid = lookuproot,
	};

	// ** defer: edbl_set(&self->lockdir, lock);
	edbl_set(handle->lockh, EDBL_ASH, lock);

	// ** defer: edbp_finish(&self->edbphandle);
	edb_err err = edbp_start(handle->edbphandle, lookuproot);
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
			if(refs[i].startoff_strait > chapter_pageoff) {
				// here is the same logic as the non-depth0 for loop but instead
				// of reference
				break;
			}
		}
		// we know that this reference contains our page in its strait.
		// So if our offset search is lets say 5, and this strait contained
		// pageoffsets 4,5,6,7,8 and associating pageids of 42,43,44,45,46.
		// that means offset 8ref46 would be the one referenced in this strait
		// and thus we subtract the end offset with the offset we know thats
		// in there and that will give us an /ref-offset/ of 2. We then
		// take the end-referance and subtrack our ref offset which gives
		// us the page offset.
		// ie: *o_pid = 46 - (8 - 5)
		//
		// also note that refc will always be non-null references.
		*o_pid = refs[i].ref - (refs[i].startoff_strait - chapter_pageoff);

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

	// So lets finish out of this page...
	edbp_finish(handle->edbphandle);
	// and release the lock
	edbl_set(handle->lockh, EDBL_ARELEASE, lock);

	// now we can recurse down to the next lookup page.
	return edba_u_lookup_rec(handle, lookuproot,
			chapter_pageoff, o_pid, depth-1);
}

edb_err edba_u_lookupoid(edba_handle_t *handle, odb_spec_index_entry *entry,
                         edb_pid chapter_pageoff, edb_pid *o_pid) {
	if(chapter_pageoff >= entry->ref0c) {
		return EDB_EEOF;
	}
	edb_err err = edba_u_lookup_rec(handle, entry->ref1, chapter_pageoff,
									o_pid,entry->memory >> 12);
#ifdef EDB_FUCKUPS
	if(*o_pid == 0) {
		log_critf("o_pid returned 0 from lookup despite it being in page "
				  "range");
	}
#endif
	return err;
}
