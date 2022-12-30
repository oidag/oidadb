#define _LARGEFILE64_SOURCE 1
#define _GNU_SOURCE 1

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <error.h>
#include <errno.h>
#include <pthread.h>

#include "options.h"
#include "include/ellemdb.h"
#include "edbd.h"
#include "errors.h"

// helper function to edb_open. returns 0 on success.
// it is assumed that path does not exist.
//
// can return EDB_EERRNO from open(2)
static edb_err createfile(const char *path, unsigned int pagemul, int flags) {
	int err = 0;
	int minpagesize = sysconf(_SC_PAGE_SIZE);

	// create the file itself.
	int fd = creat64(path, 0666);
	if(fd == -1) {
		return EDB_EERRNO;
	}

	// truncate the file to the full length of a page.
	err = ftruncate(fd, minpagesize * pagemul);
	if(err == -1) {
		int errnotmp = errno;
		close(fd);
		log_critf("ftruncate(2) returned error code: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// generate the head structure.
	edb_fhead_intro intro = {0};
	intro.magic[0] = 0xA6;
	intro.magic[1] = 0xF0;
	intro.intsize  = sizeof(int);
	intro.entrysize = sizeof(edb_entry_t);
	intro.pagesize = minpagesize;
	intro.pagemul  = pagemul;
	edb_fhead newhead = {.intro = intro};
	// zero out the rest of the structure explicitly
	bzero(&(newhead.newest), sizeof(edb_fhead) - sizeof(edb_fhead_intro));

	// generate a random newhead.intro.id
    {
		struct timeval tv;
		gettimeofday(&tv, 0);
		srand(tv.tv_sec + tv.tv_usec + getpid());
		int neededints = sizeof(newhead.intro.id) / sizeof(int);
		for (int i = 0; i < neededints; i++) {
			int rint = rand();
			*((int *)(&(newhead.intro.id[i * sizeof(int)]))) = rint;
		}
	}

	// write the head structure into the file
	ssize_t n = write(fd, &newhead, sizeof(newhead));
	if(n == -1) {
		int errnotmp = errno;
		close(fd);
		log_critf("write(2) returned error code: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}
	if(n != sizeof(newhead)) {
		close(fd);
		log_critf("write(2) failed to write out entire head during creation");
		return EDB_ECRIT;
	}

	close(fd);
	return 0;
}

// helper function to edb_open.
// validates the headintro with the current system. returns
// EDB_ENOTDB if bad magic number (probably meaning not a edb file)
// EDB_EHW if invalid hardware.
static edb_err validateheadintro(edb_fhead_intro head, int pagemul) {
	if(head.magic[0] != 0xA6 || head.magic[1] != 0xF0) {
		log_errorf("invalid magic number: got {0x%02X, 0x%02X} but expecting {0x%02X, 0x%02X}",
				   head.magic[0], head.magic[1],
				   0xA6, 0xF0);
		return EDB_ENOTDB;
	}
	if(head.intsize != sizeof(int)) {
		log_errorf("integer size mismatch: got %d but accepting %ld",
				   head.intsize,
				   sizeof(int));
		return EDB_EHW;
	}
	if(head.entrysize != sizeof(edb_entry_t)) {
		log_errorf("entry size mismatch: got %d but accepting %ld",
				   head.entrysize,
				   sizeof(edb_entry_t));
		return EDB_EHW;
	}
	if(head.pagesize != sysconf(_SC_PAGE_SIZE)) {
		log_errorf("minimum page size mismatch: got %d but accepting %ld",
				   head.pagesize,
				   sysconf(_SC_PAGE_SIZE));
		return EDB_EHW;
	}
	if(head.pagemul != 1 &&
	   head.pagemul != 2 &&
	   head.pagemul != 4 &&
	   head.pagemul != 8) {
		log_errorf("page multiplier is not an acceptable number: got %d",
		           head.pagemul);
		return EDB_EHW;
	}
	if(head.pagemul != pagemul) {
		log_errorf("page multiplier of database does not match host: database is %d, host is %d",
		           head.pagemul,
				   pagemul);
		return EDB_EHW;
	}
	return 0;
}

unsigned int edbd_size(const edbd_t *c) {
	return c->page_size;
}

void edbd_close(edbd_t *file) {

	// destroy mutex
	pthread_mutex_destroy(&file->adddelmutex);

	// dealloc memeory
	int err = munmap(file->head, sysconf(_SC_PAGE_SIZE));
	if(err == -1) {
		int errtmp = errno;
		log_critf("failed to close file descriptor from nummap(2) errno: %d", errtmp);
		errno = errtmp;
		return;
	}

	// close the descriptor
	err = close(file->descriptor);
	if(err == -1) {
		int errtmp = errno;
		log_critf("failed to close file descriptor from close(2) errno: %d", errtmp);
		errno = errtmp;
		return;
	}
}

edb_err edbd_open(edbd_t *file, const char *path, unsigned int pagemul, int flags) {

	// initialize memeory.
	bzero(file, sizeof(edbd_t));
	file->path = path;

	int err = 0;
	int trycreate = flags & EDB_HCREAT;

	// get the stat of the file, and/or create the file if params dicates it.
	restat:
	err = stat(path, &(file->openstat));
	if(err == -1) {
		int errnotmp = errno;
		if(errno == ENOENT && trycreate) {
			// file does not exist and they would like to create it.
			edb_err createrr = createfile(path, pagemul, flags);
			if(createrr != 0) {
				// failed to create
				return createrr;
			}
			// create successful, go back and perform the operation
			// with the explicit assumption that it is created.
			trycreate = 0;
			goto restat;
		}
		errno = errnotmp;
		return EDB_EERRNO;
	}

	// make sure this is a file.
	if((file->openstat.st_mode & S_IFMT) != S_IFREG) {
		return EDB_EFILE;
	}

	// open the actual file descriptor.
	//
	// I use O_DIRECT here because we will be operating our own cache and
	// operating exclusively on a block-by-block basis with mmap. Maybe
	// that's good reason?
	//
	// O_NONBLOCK - set just incase mmap(2)/read(2)/write(2) are enabled to
	//              wait for advisory locks, which they shouldn't... we manage
	//              those locks manually. (see Mandatory locking in fnctl(2)
	file->descriptor = open64(path, O_RDWR
	                                | O_DIRECT
	                                | O_SYNC
	                                | O_LARGEFILE
	                                | O_NONBLOCK);
	if(file->descriptor == -1) {
		return EDB_EERRNO;
	}

	int psize = sysconf(_SC_PAGE_SIZE);


	// lock the first page of file so that only this process can use it.
	// we enable noblock so we know if something else has it open.
	struct flock64 dbflock = {
		.l_type   = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start  = 0,
		.l_len    = psize,
		.l_pid    = 0,
	};
	// note we use OFD locks becuase the host can be started in the same
	// process as handlers, but just use different threads.
	err = fcntl64(file->descriptor, F_OFD_SETLK, &dbflock);
	if(err == -1) {
		int errnotmp = errno;
		close(file->descriptor);
		if(errno == EACCES || errno == EAGAIN)
			return EDB_EOPEN; // another process has this open.
		// no other error is possible here except for out of memory error.
		log_critf("failed to call flock(2) due to unpredictable errno: %d", errnotmp);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// file open and locked. load in the head.
	file->head = mmap64(0, psize, PROT_READ | PROT_WRITE,
	                    MAP_SHARED_VALIDATE,
	                    file->descriptor,
	                    0);
	if(file->head == MAP_FAILED) {
		int errnotmp = errno;
		log_critf("failed to call mmap(2) due to unpredictable errno: %d", errnotmp);
		close(file->descriptor);
		errno = errnotmp;
		return EDB_ECRIT;
	}

	// past this point, if we fail and need to return an error,
	// we must run only closefile(dbfile).

	// validate the head intro on the system to make sure
	// it can run on this architecture.
	{
		edb_err valdationerr = validateheadintro(file->head->intro, pagemul);
		if(valdationerr != 0) {
			edbd_close(file);
			return valdationerr;
		}
	}

	// initialize the mutex.
	err = pthread_mutex_init(&file->adddelmutex, 0);
	if(err) {
		log_critf("failed to initialize eof mutex: %d", err);
		edbd_close(file);
		return EDB_ECRIT;
	}

	// calculate the page size
	file->page_size    = file->head->intro.pagemul * file->head->intro.pagesize;

	// file has been opened/created, locked, and validated. we're done here.
	return 0;
}

// extract the reference array from the page. Returns refs per page (will include null refs).
// this function will probably be replaced by a macro.
static int getrefs(edbd_t *file, void *page, edb_deletedref_t **o_refs);

edb_err edbd_add(edbd_t *file, uint8_t straitc, edb_pid *o_id) {
#ifdef EDB_FUCKUPS
	// invals
	if(o_id == 0) {
		log_critf("invalid ops for edbp_startc");
		return EDB_EINVAL;
	}
	if(straitc == 0) {
		log_critf("call to edbd_add with straitc of 0.");
		return EDB_EINVAL;
	}
#endif

	// easy vars
	int fd = file->descriptor;
	edb_pid setid;
	edb_err err = 0;

	// new page must be created.
	// lock the eof mutex.
	// **defer: unlockeof(file);
	pthread_mutex_lock(&file->adddelmutex);
	//seek to the end and grab the next page id while we're there.
	off64_t fsize = lseek64(fd, 0, SEEK_END);
	setid = edbd_off2pid(file, fsize + 1); // +1 to put us at the start of the next page id.
	// we HAVE to make sure that fsize/id is set properly. Otherwise
	// we end up truncating the entire file, and thus deleting the
	// entire database lol.
	if(setid == 0 || fsize == -1) {
		log_critf("failed to seek to end of file");
		err = EDB_ECRIT;
		goto return_unlock;
	}

#ifdef EDB_FUCKUPS
	// double check to make sure that the file is block-aligned.
	// I don't see how it wouldn't be, but just incase.
	if(fsize % edbd_size(file) != 0) {
		log_critf("for an unkown reason, the file isn't page aligned "
		          "(modded over %ld bytes). Crash mid-write?", fsize % edbd_size(file));
		err = EDB_ECRIT
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
			err = EDB_ENOSPACE;
		} else {
			err = EDB_ECRIT;
		}
		goto return_unlock;
	}

	// As noted just a second ago, the pages we have created are nothing
	// but 0s. And by design, this means that the heads of these pages
	// are actually defined as per specification. Sense the _edbd_stdhead.ptype
	// will be 0, that means it takes the definiton of EDB_TINIT.

	// later: however, I need to think about what happens if the thing
	//        crashes mid-creating. I need to roll back all these page
	//        creats.

	*o_id = setid;

	return_unlock:
	pthread_mutex_unlock(&file->adddelmutex);
	return err;
}

edb_err edbd_del(edbd_t *file, uint8_t straitc, edb_pid id) {
#ifdef EDB_FUCKUPS
	if(straitc == 0 || id == 0) {
		log_critf("invalid arguments");
		return EDB_EINVAL;
	}
	// see the "goto retry_newpage"
	int recusiveoopsisescheck = 0;
#endif

	// some working vars.
	edb_err err = 0;
	edb_deletedref_t *ref;
	edb_deleted_refhead_t *head;
	const unsigned int refsperpage = (edbd_size(file) - EDBD_HEADSIZE) / sizeof(edb_deletedref_t);
	pthread_mutex_lock(&file->adddelmutex);

	// first look at our deleted window. Do we have any free slots?
	retry_newpage:
	for(unsigned int i = 0; i < file->delpagesc; i++) { // we go forwards to "push" deleted pages.
		head = file->delpages[i];
		if(head->refc == refsperpage) {
			// we shouldn't try to search this page because it's already completely full of non-null
			// referneces. Although it is still possible to fit these newly deleted
			// pages in here via the [[expand-existing-optimization]] (see below). We'll
			// be lazy as that optimization is unlikely to happen with already full pages.
			continue;
		}
		// with this page selected see if we have any null refs.
		for(unsigned int j = 0; j < refsperpage; j++) { // again, go forwards to push
			ref = file->delpages[i] + EDBD_HEADSIZE + sizeof(edb_deletedref_t) * j;
			if(ref->ref == 0) {
				// found a null reference
				ref->ref = id;
				ref->straitc = straitc;
				head->refc++;
				head->pagesc += straitc;
				goto return_unlock;
			}

			// [[expand-existing-optimization]]
			// this ref is non-null, can't use it... unless the pages we are needing to
			// delete just happen to be in strait to this. In which case we can just
			// edit this referance to include the strait. We'll also make sure consolidating
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
			goto return_unlock;
		}
	}
#ifdef EDB_FUCKUPS
	if(recusiveoopsisescheck) {
		log_critf("just executed delete-find logic twice. Something went wrong with the page create.");
		err = EDB_ECRIT;
		goto return_unlock;
	}
	recusiveoopsisescheck++;
#endif
	{ // scoped for readability
		// atp: This should be rare. But we didn't find any null references in our current deleted pages window.
		//      Thus, we must find/create a new window so we have some null references.

		// We will opt-in for creating a new edbp_delete page because it's unlikely that a previous
		// page in the deleted chapter isn't full... yeah it can still happen... but unlikely.
		// So just create a new page.
		edb_entry_t *ent;
		edbd_index(file, EDBD_EIDDELTED, &ent);
		edb_pid newdeletedpage;
		err = edbd_add(file, 1, &newdeletedpage);
		if (err) {
			goto return_unlock;
		}

		// reference the new page to the chapater.
		// todo: make sure to initialize delpages.
		head = file->delpages[0]; // we know index 0 will always be the current last page.
		head->head.pright = newdeletedpage;

		// move all of pages in the window 1 further down the array. The last index
		// of this window will be unloaded to make room for the newly created page.
		munmap(file->delpages[file->delpagesc-1], edbd_size(file));
		for(int i = file->delpagesc-1; i > 0; i--) {
			file->delpages[i] = file->delpages[i-1];
		}

		// load in our newly created page
		file->delpages[0] = mmap64(0, edbd_size(file), PROT_READ | PROT_WRITE,
		                           MAP_SHARED,
		                           file->descriptor,
		                           edbd_pid2off(file, newdeletedpage));
		if (file->delpages[0] == (void *) -1) {
			log_critf("failed to allocate memory for new deleted memory index");
			// todo: unlocks
			return EDB_ECRIT;
		}
		// initialize the new page
		head = file->delpages[0];
		head->head.pleft = ent->ref1;
		//head->head.pright = 0; its 0 already sense intiialized to 0
		head->head.ptype = EDB_TDEL;

		// finally, mark the newdeleted page as in the chapter from the entry's perspective
		ent->ref0c++;
		ent->ref1 = newdeletedpage;

		// now that we've just created a fresh page at index 0. We'll go back to our original
		// logic that finds null references, which now that it has a fresh page, should 100%
		// return before it trys to create yet another page. I'll put some EDB_FUCKUPS catches
		// jsut incase.
		goto retry_newpage;
	}

	return_unlock:
	pthread_mutex_unlock(&file->adddelmutex);
	return err;
}