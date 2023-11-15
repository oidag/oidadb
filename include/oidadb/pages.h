#ifndef _ODB_PAGES_H_
#define _ODB_PAGES_H_



typedef enum edb_ioflags {

	// Can only be read
	ODB_PRDONLY = 1,

	//  Can only be wrote
	ODB_PWRONLY = 2,

	// Can read and write
	ODB_PRDWR = 3,

	// Puts the function into create mode. The pages are created and the first page id
	// is set to the page output variable.
	ODB_PCREAT = 4,

	// For odbh_page_open - This will prevent anything else from modifying the page so long that
	// the page is open. If this is false, then when the page is closed, and it was modify elsewhere,
	// a merge will be needed.
	ODB_PEXCL = 8,


	//
	ODB_PCOMMIT,
	ODB_PROLLBACK,
} odb_ioflags;

// odb_open_file - is way faster than upstream, but only works with block devices


export odb_err odb_open(const char *file, odb_ioflags flags, odbh *o_descriptor);

// exposes a downstream. sockfd must be ready for calling accept(2). bind(2) and listen(2) should already be called.
// the socket must be 2 way and blocking.

export odb_err odb_close(odbh *o_descriptor);


export odb_err odbh_checkout(odbh *handle);

export odb_err odbh_use(odbh *handle, odb_ioflags flags, int strait, odb_pid *page);

// must call odbh_use with the pid before you can get it via odbh_page
export void *odbh_page(odbh *handle, odb_pid page);

// returns the number of pages found in o_pagev. pagev and straitv are assocative
export int odbh_conflicts(odbh *handle, odb_pid *o_pagev, int *straitv);

// resolve marks the page as resolved
export odb_err odbh_resolve(odbh *handle, int strait, odb_pid page);

// revert all changes and checkin (closes a checkout)
export odb_err odbh_rollback(odbh *handle);

// attempts to apply all changes. If you attempt to save that had been saved elsewhere, a conflict arises.
// the conflict must merge itself. You can do this with pmerge.
//
// when commit sees a conflict it sees what pages need to be merged.
export odb_err odbh_commit(odbh *handle);




#endif
