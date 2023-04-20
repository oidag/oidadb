
#include "../edbd.h"
#include "../include/oidadb.h"
#include "teststuff.h"


#include <stdio.h>
int newdeletedpages = 0;
odb_err _pid_odbtelem_attach(pid_t hostpid);
void test_main() {
	struct odb_createparams createparams  =odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if(err) {
		test_error("failed to create file");
		return;
	}

	// open the file
	int fd = open(test_filenmae, O_RDWR);
	if(fd == -1) {
		test_error("bad fd");
		return;
	}

	if(err) {
		test_error("odbtelem");
		return;
	}
	//odbtelem_bind(ODBTELEM_PAGES_NEWDEL, newdel);

	edbd_t dfile;
	edbd_config config;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if(err) {
		test_error("edbd_open failed");
		return;
	}

	//err = _pid_odbtelem_attach(getpid());
	if(err) {
		test_error("odbtelem_attach");
		return;
	}

	// testing configs
	const int pagec = 16;
	const int page_strait = 1;
	const int page2c = 4;
	const int page2_strait = 4;

	// working vars.
	odb_pid pages[pagec];
	odb_pid large_strait_pages[page2c];

	// create pages
	for(int i = 0; i < pagec; i++) {
		err = edbd_add(&dfile, page_strait, &pages[i]);
		if(err) {
			test_error("edbd_add 1");
			goto ret;
		}
	}
	// create pages2
	for(int i = 0; i < page2c; i++) {
		err = edbd_add(&dfile, page2_strait, &large_strait_pages[i]);
		if(err) {
			test_error("edbd_add 2");
			goto ret;
		}
	}

	// for a new file. We should now have exactly pagetotal unreferenced
	// pages

	// trash all the pages we just created but do not do
	for(int i = 0; i < pagec; i++) {
		err = edbd_del(&dfile, page_strait, pages[i]);
		if(err) {
			test_error("edbd_del 1");
			goto ret;
		}
	}
	for(int i = 0; i < page2c; i++) {
		err = edbd_del(&dfile, page2_strait, large_strait_pages[i]);
		if(err) {
			test_error("edbd_del 2");
			goto ret;
		}
	}

	// check in the odb_deleted chapter to make sure all pages were deleted.
	{
		odb_spec_index_entry *oid_deleted;
		edbd_index(&dfile, EDBD_EIDDELTED, &oid_deleted);
		newdeletedpages = oid_deleted->ref0c;
		if (lseek(fd, edbd_pid2off(&dfile, oid_deleted->ref0), SEEK_SET) ==
		    -1) {
			test_error("lseek");
			goto ret;
		}
		// we'll just read the first edb_deleted page.
		const int pagetotal = pagec * page_strait + page2c * page2_strait;
		odb_spec_deleted header;
		read(fd, &header, sizeof(odb_spec_deleted));
		if (header.pagesc != pagetotal) {
			test_error("first page of deleted chapter does not contain the "
			           "expected amount of pages: expected %d, got %d (delta %d)",
			           pagetotal, header.pagesc,
			           pagetotal - (int32_t) header.pagesc);
			goto ret;
		}
	}


	// close the file.
	edbd_close(&dfile);


	// file size should be exact expected size.
	struct stat fstat;
	if(stat(test_filenmae, &fstat) == -1) {
		test_error("failed to stat file");
		goto ret;
	}
	// calculated outputs to measure.
	// pagetotal - the total amount of pages we just created.
	{
		const int pagetotal =
				pagec * page_strait + page2c * page2_strait + newdeletedpages;
		const int pagetotal_including_header = pagetotal
		                                       + 1 // (for header)
		                                       + createparams.indexpages
		                                       + createparams.structurepages;
		const uint64_t bytetotal = pagetotal_including_header
		                           * createparams.page_multiplier
		                           * sysconf(_SC_PAGE_SIZE);

		if (fstat.st_size != bytetotal) {
			test_error(
					"byte total not expected size: expected %ld, got %ld (%ld "
					"delta)",
					bytetotal,
					fstat.st_size,
					(off_t) bytetotal - fstat.st_size);
			goto ret;
		}
	}

	ret:
	return;
}