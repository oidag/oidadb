#define _GNU_SOURCE

#include "options.h"
#include "telemetry.h"
#include <oidadb/oidadb.h>
#include "edbd.h"
#include "errors.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

// same as edbd_add but no mutex/locking controls. Seperated because of its use in
// both edbd_add and edbd_del
static odb_err _edbd_add(edbd_t *file, uint8_t straitc, odb_pid *o_id) {
	// easy vars
	int fd = file->descriptor;
	odb_pid setid;
	odb_err err = 0;
	odb_spec_deleted_ref *ref;
	const unsigned int refsperpage = (edbd_size(file) - ODB_SPEC_HEADSIZE) / 
			sizeof(odb_spec_deleted_ref);


	// first look at our edbp_deleted window see if we already have some pages that we
	// can recycle.
	for(unsigned int i = 0; i < file->delpagesc; i++) {
		odb_spec_deleted *head = file->delpagesv[i];
		int newlargeststrait = 0;
		if (head->largeststrait < straitc) {
			// no references in this edbp_deleted have enough space for us so we can quickly skip it.
			continue;
		}

		// we set this to zero so we can detect if we found a match in the
		// next loop
		*o_id = 0;

		// with this page selected in head, we can loop through the refs
		for (unsigned int j = 0; j < refsperpage; j++) {

			// later: an optimization that can be made is make sure that we only use a reference
			//        that has a straitc close (but not under) the requested straitc. This is
			//        to make sure that page creations with small straitcs don't peck away at
			//        the larger references in leu of killing out more references (thus using
			//        fewer edbp_deleted pages).
			//        Note that this j-loop will always go through (it will never break) so this
			//        optimization won't hurt anything.

			ref = file->delpagesv[i] + ODB_SPEC_HEADSIZE + sizeof(odb_spec_deleted_ref) * j;
			if(*o_id != 0 || ref->ref == 0 || ref->straitc < straitc) {

				// if we're passing up this reference that doesn't mean it could be the next
				// largest reference.
				if(newlargeststrait < ref->straitc) {
					newlargeststrait = ref->straitc;
				}

				// this reference is either null or not enough pages to get our strait.
				continue;
			}

			// we found a reference we can steal some pages from.
			*o_id = ref->ref;
			ref->straitc -= straitc;
			head->pagesc -= straitc;

			// If we emptied out this ref we need to knock it off the head's count.
			// Otherwise we just increment the ref to point to further down into
			// the strait.
			if(ref->straitc == 0) {
				ref->ref = 0;
				head->refc--;
			} else {
				ref->ref += straitc;
			}

			// we do the calculation we did for all the other ones.
			if(newlargeststrait < ref->straitc) {
				newlargeststrait = ref->straitc;
			}
		}

		if(*o_id != 0) {

			// we found pages we can recycle.

			// it is in this if loop as to why we cannot guarentee 0-initialized pages.

			// but before we can close out of our busienss here with this edbp_deleted is
			// to make sure its largeststrait is still true.
			head->largeststrait = newlargeststrait;

			goto return_unlock;
		}

		// we didn't find any pages we can recyle in this edbp_deleted page. On to the next one.
	}

	// At this point we know that a new page must be created.
	// We failed to recycle a previously deleted page.

	// However, we may have some bookkeeping to do in the fact that we may need to shift
	// the deleted page window to the left (closer to the beginning)
	// But we have an edge case in the chance that there exist no deleted
	// page in the chapter yet.
	if(file->delpagesc) {
		odb_spec_deleted *firstpage = file->delpagesv[file->delpagesc - 1];
		odb_spec_deleted *lastpage = file->delpagesv[0];
		if (firstpage->head.pleft != 0 && lastpage->refc == 0) {
			// we know that our window CAN be moved to the left and we know our last
			// page has no more references. So we can move the window to the right.

			// unmap the rightmost (latest and empty) page. And make it leave our window.
			munmap(lastpage, edbd_size(file));

			// move all the pages
			for(int i = 0; i < file->delpagesc - 1; i++) {
				file->delpagesv[i] = file->delpagesv[i + 1];
			}

			// mmap the existing edbp_deleted page thats to the left of our window,
			// bringing it into the window
			file->delpagesv[file->delpagesc - 1] = mmap64(0, edbd_size(file), PROT_READ | PROT_WRITE,
			                                              MAP_SHARED,
			                                              file->descriptor,
			                                              edbd_pid2off(file, firstpage->head.pleft));
		}
		// all potential bookkeeping done.
	}

	//seek to the end and grab the next page id while we're there.
	off64_t fsize = lseek64(fd, 0, SEEK_END);
	setid = edbd_off2pid(file, fsize + 1); // +1 to put us at the start of the next page id.
	// we HAVE to make sure that fsize/id is set properly. Otherwise
	// we end up truncating the entire file, and thus deleting the
	// entire database lol.
	if(setid == 0 || fsize == -1) {
		log_critf("failed to seek to end of file");
		err = ODB_ECRIT;
		goto return_unlock;
	}

#ifdef EDB_FUCKUPS
	// double check to make sure that the file is block-aligned.
	// I don't see how it wouldn't be, but just incase.
	if(fsize % edbd_size(file) != 0) {
		log_critf("for an unkown reason, the file isn't page aligned "
		          "(modded over %ld bytes). Crash mid-write?", fsize % edbd_size(file));
		err = ODB_ECRIT;
		goto return_unlock;
	}
#endif

	// using ftruncate we expand the size of the file by the amount of
	// pages they want. As per ftruncate(2), this will initialize the
	// pages with 0s.
	int werr = ftruncate64(fd, fsize + edbd_size(file) * straitc);
	if(werr == -1) {
		int errnom = errno;
		log_critf("failed to truncate-extend for new pages");
		if(errnom == EINVAL) {
			err = ODB_ENOSPACE;
		} else {
			err = ODB_ECRIT;
		}
		goto return_unlock;
	}

	// As noted just a second ago, the pages we have created are nothing
	// but 0s. And by design, this means that the heads of these pages
	// are actually defined as per specification. Sense the _edbd_stdhead.ptype
	// will be 0, that means it takes the definiton of ODB_ELMINIT.

	// later: however, I need to think about what happens if the thing
	//        crashes mid-creating. I need to roll back all these page
	//        creats.

	*o_id = setid;

	return_unlock:
	return err;
}

odb_err edbd_add(edbd_t *file, uint8_t straitc, odb_pid *o_id) {
#ifdef EDB_FUCKUPS
	// invals
	if(o_id == 0) {
		log_critf("invalid ops for edbp_startc");
		return ODB_EINVAL;
	}
	if(straitc == 0) {
		log_critf("call to edbd_add with straitc of 0.");
		return ODB_EINVAL;
	}
#endif

	odb_err err;

	// lock the eof mutex.
	pthread_mutex_lock(&file->adddelmutex);
	// **defer: pthread_mutex_unlock(file);

	err = _edbd_add(file, straitc, o_id);

	pthread_mutex_unlock(&file->adddelmutex);
	return err;
}

odb_err edbd_del(edbd_t *file, uint8_t straitc, odb_pid id) {
#ifdef EDB_FUCKUPS
	if(straitc == 0 || id == 0) {
		log_critf("invalid arguments");
		return ODB_EINVAL;
	}
	// see the "goto retry_newpage"
	int recusiveoopsisescheck = 0;
#endif

	// some working vars.
	odb_err err = 0;
	odb_spec_deleted_ref *ref;
	odb_spec_deleted *head;
	const unsigned int refsperpage = (edbd_size(file) - ODB_SPEC_HEADSIZE) / sizeof(odb_spec_deleted_ref);
	pthread_mutex_lock(&file->adddelmutex);

	// first look at our deleted window. Do we have any free slots?
	retry_newpage:
	for(unsigned int i = 0; i < file->delpagesc; i++) { // we go forwards to "push" deleted pages.
		head = file->delpagesv[i];
		if(head->refc == refsperpage) {
			// we shouldn't try to search this page because it's already completely full of non-null
			// referneces. Although it is still possible to fit these newly deleted
			// pages in here via the [[expand-existing-optimization]] (see below). We'll
			// be lazy as that optimization is unlikely to happen with already full pages.
			continue;
		}
		// with this page selected see if we have any null refs.
		for(unsigned int j = 0; j < refsperpage; j++) {
			ref = file->delpagesv[i] + ODB_SPEC_HEADSIZE + sizeof(odb_spec_deleted_ref) * j;
			if(ref->ref == 0) {
				// found a null reference
				ref->ref = id;
				ref->straitc = straitc;
				head->refc++;
				head->pagesc += straitc;
				if(head->largeststrait < ref->straitc) {
					head->largeststrait = ref->straitc;
				}
				goto return_unlock;
			}

			// [[expand-existing-optimization]]
			// this ref is non-null, can't use it... unless the pages we are needing to
			// delete just happen to be sitting right next to this ref. In which case we can just
			// edit this referance to expand the strait. We'll also make sure consolidating
			// these straits doesn't overflow a uint16. The next paragraph of code is but a
			// neat little optimization.
			if(((int)ref->straitc+(int)straitc) < UINT16_MAX) {
				continue;
			}
			if(ref->ref + ref->straitc == id) {
				ref->straitc += straitc;
				head->pagesc += straitc;
			} else if(id + straitc == ref->ref) {
				ref->straitc += straitc;
				head->pagesc += straitc;
				ref->ref      = id;
			} else {
				// can't tag along with this ref.
				continue;
			}
			// atp: we didn't find a null ref because we found a ref that we can just "tag along" with.
			//      Meaning we expanded an existing refernce to include our deleted pages.
			if(head->largeststrait < ref->straitc) {
				head->largeststrait = ref->straitc;
			}
			goto return_unlock;
		}
	}
#ifdef EDB_FUCKUPS
	if(recusiveoopsisescheck) {
		log_critf("just executed delete-find logic twice. Something went wrong with the page create.");
		err = ODB_ECRIT;
		goto return_unlock;
	}
	recusiveoopsisescheck++;
#endif
	{ // scoped for readability

		// atp: This should be rare. But we didn't find any null references
		//      in our current deleted pages window. Thus, we must move the
		//      window so we have some null references in it. Or, expand the
		//      chapter and move the window further out into the new territory.

		// We will opt-in for creating a new edbp_delete page because it's
		// unlikely that a previous left-of-window page in the deleted
		// chapter isn't full... yeah it can still happen... but unlikely.
		// So just create a new page.
		odb_spec_index_entry *ent;
		edbd_index(file, EDBD_EIDDELTED, &ent);
		odb_pid newdeletedpage;

		// initialize will dictate if we need to initialize the odb_deleted when
		// we load it. It will also mean we must update our entry.
		int initialize = 0;

		if(!file->delpagesc) {
			// no deleted pages at all. Thus we must create one.
			newdeletedpage = 0;
		} else {
			odb_spec_deleted *delhead = file->delpagesv[0];
			// note: even though we're not in the else-block, that doesn't mean
			// newdeletepage is non-0.
			newdeletedpage = delhead->head.pright;
		}

		// do we have anything sitting to the right of our window?...
		if(!newdeletedpage) {
			// ....no, we need to expand the deletion referance chapter.

			// Yes. We are using the edbd_add function in the edbd_del function. If I were to
			// go into the technical details of WHY this works, it would become very uninteresting.
			// Ie: edbd_add has the potential to modify file->delpages and file->delapgesc.
			//
			// Just trust me, I worked it out, this works, and it works well.
			err = _edbd_add(file, 1, &newdeletedpage);
			if (err) {
				goto return_unlock;
			}

			// sense we're dealing with a new page, we must initialize it.
			initialize = 1;
		}

		// atp: newdeletedpage is a pid that must become the new [0] index of
		//      delpagesv.

		// before we do the shift, if we're adding a new initialized chapter
		// then we must forward-reference this page from the currently
		// right-most page.
		if(initialize && file->delpagesc) {
#ifdef EDB_FUCKUPS
			if(ent->ref1 && file->delpagesc == 2) {
				head = file->delpagesv[1];
				if(ent->ref1 != head->head.pright) {
					log_critf("The deleted page window is not in order, or "
							  "the entry is corrupted. delpagesv[1].pright !="
							  " ent->ref1 when deletion window has detected "
							  "that a new page must be created");
				}
			}
#endif
			head = file->delpagesv[0];
			head->head.pright = newdeletedpage;
		}

		// move the window to the right to make our newly created deleted
		// page visiable.
		if(file->delpagesc == file->delpagesq) {
			// The last index of this window will be unloaded to make room to
			// shif our window. This is removing the right-most (oldest) page.
			munmap(file->delpagesv[file->delpagesc - 1], edbd_size(file));
		} else {
			// No need to pop the highest buffer sense we have space
			// remaining.
			file->delpagesc++;
		}

		// shift the window to the right.
		// note: this for loop does NOT touch file->delpagesv[0] (i > 0) not
		//       (i >= 0).
		// note: in the case of delpagesc == 0, i will be -1 which will not loop
		// note: see ((early-munmap)) to see what happened to
		//       delpagesv[file->delpagesc-1].
		for(int i = (int)file->delpagesc-1; i > 0; i--) {
			file->delpagesv[i] = file->delpagesv[i - 1];
		}

		// load in our new right-of-window page.
		file->delpagesv[0] = mmap64(0,
									edbd_size(file),
									PROT_READ | PROT_WRITE,
		                            MAP_SHARED,
		                            file->descriptor,
		                            edbd_pid2off(file, newdeletedpage));
		if (file->delpagesv[0] == (void *) -1) {
			log_critf("failed to allocate memory for new deleted memory index");
			err = ODB_ECRIT;
			goto return_unlock;
		}

		if(initialize) {

			// head to be initialized
			head = file->delpagesv[0];

			// set the newdeletedpage's pleft head to the last most up to
			// date page.
			head->head.pleft = ent->ref1;

			//head->head.pright = 0; its 0 already sense intiialized to 0
			head->head.ptype = ODB_ELMTRASH;

			// if this is the first page of the chapter then we must mark it
			// in ref0.
			if(!ent->ref0c) {
				ent->ref0 = newdeletedpage;
			}

			// finally, mark the newdeleted page as in the chapter from the
			// entry's perspective
			ent->ref0c++;
			ent->ref1 = newdeletedpage;
			telemetry_pages_newdel(newdeletedpage);
		}

		// now that we've just created a fresh page at index 0. We'll go back to our original
		// logic that finds null references, which now that it has a fresh page, should 100%
		// return before it trys to create yet another page. I'll put some EDB_FUCKUPS catches
		// just incase.
		goto retry_newpage;
	}

	return_unlock:
	pthread_mutex_unlock(&file->adddelmutex);
	return err;
}