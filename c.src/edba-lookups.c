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
	edbl_lockref lock = (edbl_lockref){
			.l_type = EDBL_TYPSHARED,
			.l_len = 1,
			.l_start = edbd_pid2off(handle->edbphandle.parent->fd, lookuproot),
	};

	// ** defer: edbl_set(&self->lockdir, lock);
	edbl_set(&handle->lockh, lock);
	lock.l_type = EDBL_TYPUNLOCK; // doing this in advance

	// ** defer: edbp_finish(&self->edbphandle);
	edb_err err = edbp_start(&handle->edbphandle, lookuproot);
	if(err) {
		edbl_set(&handle->lockh, lock);
		return err;
	}
	// set the lookup hint now
	// generate the proper EDBP_HINDEX... value
	edbp_hint h = EDBP_HINDEX0 - depth * 0x10;
	edbp_mod(&handle->edbphandle, EDBP_CACHEHINT, h);
	edbp_lookup_t *l = edbp_glookup(&handle->edbphandle);
	edb_lref_t *refs = edbp_lookup_refs(l);

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
		edbp_finish(&handle->edbphandle);
		// and release the lock
		edbl_set(&handle->lockh, lock);
		return 0;
	}

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

	// at this point, we know that refs[i] is the reference we must follow.
	// Lets throw the important number in our stack.
	edb_pid nextstep = refs[i].ref;

	// So lets finish out of this page...
	edbp_finish(&handle->edbphandle);
	// and release the lock
	edbl_set(&handle->lockh, lock);

	// now we can recurse down to the next lookup page.
	return edba_u_lookup_rec(handle, lookuproot,
			chapter_pageoff, o_pid, depth-1);
}

edb_err edba_u_lookupoid(edba_handle_t *handle, edb_entry_t *entry,
                         edb_pid chapter_pageoff, edb_pid *o_pid) {
	if(chapter_pageoff >= entry->ref0c) {
		return EDB_EEOF;
	}
	return edba_u_lookup_rec(handle, entry->ref1, chapter_pageoff, o_pid, entry->memory >> 12);
}







}
