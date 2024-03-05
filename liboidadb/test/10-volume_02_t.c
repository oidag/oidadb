#include "teststuff.h"

#include <oidadb/pages.h>
#include <oidadb/buffers.h>
#include <wait.h>

#include "../errors.h"


/*
 The purpose of this test is to test the behaviour of checkouts, commits,
 and buffers.
 */

const int buffer_size = 21;


const int processes = 9;

void test_main() {

	// create a fresh database
	odb_desc *desc;
	err = odb_open(test_filenmae, ODB_PREAD | ODB_PWRITE | ODB_PCREAT, &desc);
	if(err) {
		test_error("odb_open");
		return;
	}
	odb_close(desc);

	int is_host = 1;
	for(int i = 0; i < processes; i++) {
		pid_t pid = fork();
		if(!pid) {
			test_log("child %d starting", getpid());
			is_host = 0;
			break;
		}
	}

	err = odb_open(test_filenmae, ODB_PREAD | ODB_PWRITE, &desc);
	if(err) {
		test_error("odb_open2");
		return;
	}

	// create a new buffer with the leng

	struct odb_buffer_info binf = {
			.flags = ODB_UCOMMITS,
			.bcount = buffer_size,
	};
	odb_buf *buf;

	if((err = odbh_buffer_new(binf, &buf))) {
		test_error("buffer new");
		return;
	}

	if((err = odbp_bind_buffer(desc, buf))) {
		test_error("bind buffer");
		return;
	}

	for(int i = 0; i < 0x10; i++) {

		if ((err = odbp_seek(desc, 0))) {
			test_error("seek4");
			return;
		}

		if ((err = odbp_checkout(desc, 12))) {
			test_error("checkout4");
			return;
		}

		int *pagedata;

		odbh_buffer_map(buf, (void **)&pagedata, 0, 12);
		for(int i = 0; i < (ODB_PAGESIZE*12)/sizeof(int); i++) {
			pagedata[i]++;
		}
		odbh_buffer_unmap(buf, 0, 12);

		if ((err = odbp_commit(desc, 12))) {
			if (err == ODB_EVERSION) {
				err = 0;
				test_log("%d: ODB_EVERSION, attempting re-commit", getpid());
				i--;
				continue;
			}
			test_error("commit4");
			return;
		}
	}


	odbh_buffer_free(buf);
	odb_close(desc);

	if(is_host) {
		for(int i = 0; i < processes; i++) {
			int ret;
			pid_t pid = wait(&ret);
			test_log("host: child %d closed with status %d", pid, ret);
			if(ret) {
				test_error("child closed non-0");
			}
		}
	} else {
		return;
	}


	// let the host do the testing

	err = odb_open(test_filenmae, ODB_PREAD, &desc);
	if(err) {
		test_error("odb_open2");
		return;
	}

	binf.flags = 0;

	if((err = odbh_buffer_new(binf, &buf))) {
		test_error("buffer new");
		return;
	}

	if((err = odbp_bind_buffer(desc, buf))) {
		test_error("bind buffer");
		return;
	}

	if ((err = odbp_seek(desc, 0))) {
		test_error("seek4");
		return;
	}

	if ((err = odbp_checkout(desc, 12))) {
		test_error("checkout4");
		return;
	}

	int *pagedata;

	odbh_buffer_map(buf, (void **)&pagedata, 0, 12);

	// Here, we test that transactions were executed atomically. That despite
	// us having multiple processes all trying to increment the same values,
	// the values are equal to the total amount of incrementing that would
	// have happened if we had but 1 process doing it all.

	for(int i = 0; i < (ODB_PAGESIZE*12)/sizeof(int); i++) {
		if(pagedata[i] != (processes+1) * 0x10) {
			test_error("unexpected value");
		}
	}
	odbh_buffer_unmap(buf, 0, 12);

	odbh_buffer_free(buf);
	odb_close(desc);
}