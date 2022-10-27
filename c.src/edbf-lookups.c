

// converts a pid offset to the actual page address
// note to self: the only error returned by this should be a critical error
//
// assumptions:
//     pidoffset_search is less than the total amount of pages in the edbp_object chapter
edb_err static rowoffset_lookup(edb_worker_t *self,
                                int depth,
                                edb_pid lookuppage,
                                edb_pid pidoffset_search,
                                edb_pid *o_pid) {


	// install sh lock on first byte of page per Object-Reading spec
	edbl_lockref lock = (edbl_lockref){
			.l_type = EDBL_TYPSHARED,
			.l_len = 1,
			.l_start = edbp_pid2off(self->cache, lookuppage),
	};
	// ** defer: edbl_set(&self->lockdir, lock);
	edbl_set(&self->lockdir, lock);
	lock.l_type = EDBL_TYPUNLOCK; // doing this in advance

	// ** defer: edbp_finish(&self->edbphandle);
	edb_err err = edbp_start(&self->edbphandle, &lookuppage); // (ignore error audaciously)
	if(err) {
		edbl_set(&self->lockdir, lock);
		return err;
	}
	// set the lookup hint now
	// generate the proper EDBP_HINDEX... value
	edbp_hint h = EDBP_HINDEX0 - depth * 0x10;
	edbp_mod(&self->edbphandle, EDBP_CACHEHINT, h);
	edbp_lookup_t *l = edbp_glookup(&self->edbphandle);
	edb_lref_t *refs = edbp_lookup_refs(l);

	if(depth == 0) {
		// if depth is 0 that means this page is full of leaf node references.
		// more specifically, full of leaf node /page strait/ references.
		// We know that pidoffset_search is somewhere in one of these straits.
		int i;
		for(i = 0; i < l->refc; i++) {
			// it is now the END offset
			if(refs[i].startoff_strait > pidoffset_search) {
				// here is the same logic as the non-depth0 for loop but instead
				// of reference
				break;
			}
		}
		// todo: what if ref is 0? (note to self: refc will only ever be non-null referenese
		// we know that this reference contains our page in its strait.
		// So if our offset search is lets say 5, and this strait contained
		// pageoffsets 4,5,6,7,8 and associating pageids of 42,43,44,45,46.
		// that means offset 8ref46 would be the one referenced in this strait
		// and thus we subtract the end offset with the offset we know thats
		// in there and that will give us an /ref-offset/ of 2. We then
		// take the end-referance and subtrack our ref offset which gives
		// us the page offset.
		// ie: *o_pid = 46 - (8 - 5)
		*o_pid = refs[i].ref - (refs[i].startoff_strait - pidoffset_search);

		// So lets finish out of this page...
		edbp_finish(&self->edbphandle);
		// and release the lock
		edbl_set(&self->lockdir, lock);
		return 0;
	}

	int i;
	for(i = 0; i < l->refc; i++) {
		if(i+1 == l->refc || refs[i+1].startoff_strait > pidoffset_search) {
			// logically, if we're in here that means the next interation (i+1)
			// will be the end ouf our reference list, or, will be a reference
			// that has a starting offset that is larger than this current
			// iteration. Thus, we can logically deduce that our offset is somewhere
			// down in this iteration.
			//
			// To clear out some scope, lets break out and continue
			break;
		}
		// todo: what if refs[i].ref is 0?
	}
	// note: based on our logic in the if statement, i will never equal l->refc.

	// at this point, we know that refs[i] is the reference we must follow.
	// Lets throw the important number in our stack.
	edb_pid nextstep = refs[i].ref;

	// So lets finish out of this page...
	edbp_finish(&self->edbphandle);
	// and release the lock
	edbl_set(&self->lockdir, lock);

	// now we can recurse down to the next lookup page.
	rowoffset_lookup(self,
	                 depth-1,
	                 nextstep,
	                 pidoffset_search,
	                 o_pid);
	return 0;
}
