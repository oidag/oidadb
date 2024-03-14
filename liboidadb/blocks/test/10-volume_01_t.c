#include "teststuff.h"

#include <oidadb/blocks.h>
#include <oidadb/buffers.h>

#include "../errors.h"


/*
 The purpose of this test is to test the behaviour of checkouts, commits,
 and buffers.
 */

const int buffer_size = 21;

void test_main() {

	// open a fresh new database with write abilities.

	odb_desc *desc;
	err = odb_open(test_filenmae, ODB_PREAD | ODB_PWRITE | ODB_PCREAT, &desc);
	if(err) {
		test_error("odb_open");
		return;
	}

	// create a new buffer with the leng

	struct odb_buffer_info binf = {
			.flags = ODB_UCOMMITS,
			.bcount = buffer_size,
	};
	odb_buf *buf;

	if((err = odb_buffer_new(binf, &buf))) {
		test_error("buffer new");
		return;
	}

	if((err = odbb_bind_buffer(desc, buf))) {
		test_error("bind buffer");
		return;
	}

	// lets write some data in somewhat of a chaotic manner

	for(int i = 0; i < 1024; i++) {

		if ((err = odbb_seek(desc, i))) {
			test_error("seek0");
			return;
		}

		if ((err = odbb_checkout(desc, buffer_size))) {
			test_error("checkout0");
			return;
		}

		// map the first half of the buffer and write stuff to it.
		char *pagedata;
		if((err = odbv_buffer_map(buf, (void **) &pagedata, 0, buffer_size / 2))) {
			test_error("map0");
			return;
		}
		for(int j = 0; j < ODB_PAGESIZE*buffer_size/2; j++) {
			if (pagedata[j] == 0) {
				pagedata[j] = 'a';
			}
			pagedata[j]++;
		}
		// map the second half
		if((err = odbv_buffer_map(buf, (void **) &pagedata, buffer_size / 2,
				buffer_size - buffer_size / 2))) {
			test_error("2map0");
			return;
		}
		for(int j = 0; j < ODB_PAGESIZE*buffer_size/2; j++) {
			if (pagedata[j] == 0) {
				pagedata[j] = 'A';
			}
			pagedata[j]++;
		}
		// unmap both halves
		if((err = odbv_buffer_unmap(buf, 0, buffer_size))) {
			test_error("unmap0");
			return;
		}

		if (i == 1023) {
			// last iteration... mark the last page
			if((err = odbv_buffer_map(buf, (void **) &pagedata, buffer_size - 1
			                          , 1))) {
				test_error("3map0");
				return;
			}
			for(int j = 0; j < ODB_PAGESIZE; j++) {
				pagedata[j] = '@';

			}
			if((err = odbv_buffer_unmap(buf, buffer_size - 1, 1))) {
				test_error("2unmap0");
				return;
			}
		}

		// commit the changes
		if ((err = odbb_commit(desc, buffer_size))) {
			test_error("commit0");
			return;
		}
	}

	// mark the last page


	// close the database completely.

	if ((err = odb_buffer_free(buf))) {
		test_error("close0");
		return;
	}
	odb_close(desc);

	// reopen the database in read-only mode.

	if((err = odb_open(test_filenmae, ODB_PREAD, &desc))) {
		test_error("odb_open1");
		return;
	}
	// set buffer to not have commits
	binf.flags &= ~ODB_UCOMMITS;
	if((err = odb_buffer_new(binf, &buf))) {
		test_error("buffer new1");
		return;
	}
	if((err = odbb_bind_buffer(desc, buf))) {
		test_error("bind buffer1");
		return;
	}

	// read some data and make sure its the expected value.
	for(int i = 0; i < 1024; i++) {
		if ((err = odbb_seek(desc, i))) {
			test_error("seek1");
			return;
		}

		if ((err = odbb_checkout(desc, buffer_size))) {
			test_error("checkout1");
			return;
		}

		char *pagedata;

		if((err = odbv_buffer_map(buf, (void **) &pagedata, 0, buffer_size))) {
			test_error("map1");
			return;
		}

		// all comments past this point will make sense only when you look at
		// the source code of when we wrote the data above.

		// the first block should just be 'b's
		if(i == 0) {
			for(int j = 0; j < ODB_BLOCKSIZE; j++) {
				if(pagedata[j] != 'b') {
					test_error("data was not written");
					return;
				}
			}
		}

		if(i == 1022) {
			for(int j = 0; j < ODB_BLOCKSIZE/2; j++) {
				if(pagedata[j] != 'W') {
					test_error("data was not written");
					return;
				}
			}
		}

		if((err = odbv_buffer_unmap(buf, 0, buffer_size))) {
			test_error("map1");
			return;
		}
	}

	odb_buffer_free(buf);
	odb_close(desc);
}