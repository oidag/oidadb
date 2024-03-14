#include "teststuff.h"

#include <oidadb/blocks.h>
#include <oidadb/buffers.h>

#include "../errors.h"


/*
 The purpose of this test is to simply make sure that the most basic
 operations regarding volumes, buffers, checkouts, and commit do not return
 errors.

 Does NOT test mutli-processing
 Does NOT test data validation
 */

void test_main() {
	odb_desc *desc;
	err = odb_open(test_filenmae, ODB_PREAD | ODB_PWRITE | ODB_PCREAT, &desc);
	if(err) {
		test_error("odb_open");
		return;
	}

	struct odb_buffer_info binf = {
			.flags = ODB_UCOMMITS,
			.bcount = 12,
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

	// the following for-loops are mainly arbitrary as far as what pages are
	// checked out and committed... just making sure I get all the edge cases.

	// hit block 1 only EE times

	for(int i = 0; i < 0xEE; i++) {

		if ((err = odbp_seek(desc, 0))) {
			test_error("seek0");
			return;
		}

		if ((err = odbp_checkout(desc, 1))) {
			test_error("checkout0");
			return;
		}

		if ((err = odbp_commit(desc, 1))) {
			test_error("commit0");
			return;
		}
	}

	// hit blocks 12 through 12+0xEE in 12 block operations increments

	for(int i = 0; i < 0xEE; i++) {

		if ((err = odbp_seek(desc, 12+i))) {
			test_error("seek1");
			return;
		}

		if ((err = odbp_checkout(desc, 12))) {
			test_error("checkout1");
			return;
		}

		if ((err = odbp_commit(desc, 12))) {
			test_error("commit1");
			return;
		}
	}

	// oerations on all items in group 1

	for(int i = 0; i < 1023; i++) {

		if ((err = odbp_seek(desc, i))) {
			test_error("seek2");
			return;
		}

		if ((err = odbp_checkout(desc, 1))) {
			test_error("checkout2");
			return;
		}

		if ((err = odbp_commit(desc, 1))) {
			test_error("commit2");
			return;
		}
	}

	// operation on group 2 block 1

	for(int i = 0; i < 0xEE; i++) {

		if ((err = odbp_seek(desc, 1023))) {
			test_error("seek3");
			return;
		}

		if ((err = odbp_checkout(desc, 1))) {
			test_error("checkout3");
			return;
		}

		if ((err = odbp_commit(desc, 1))) {
			test_error("commit3");
			return;
		}
	}

	// mutli-group, multi-page operation

	for(int i = 0; i < 0xAA; i++) {

		if ((err = odbp_seek(desc, 1013))) {
			test_error("seek4");
			return;
		}

		if ((err = odbp_checkout(desc, 12))) {
			test_error("checkout4");
			return;
		}

		if ((err = odbp_commit(desc, 12))) {
			test_error("commit4");
			return;
		}
	}

	// just a big fat crazy operation
	for(int i = 0; i < 2046; i++) {
		if ((err = odbp_seek(desc, i))) {
			test_error("seek5");
			return;
		}

		if ((err = odbp_checkout(desc, 11))) {
			test_error("checkout5");
			return;
		}

		if ((err = odbp_commit(desc, 8))) {
			test_error("commit5");
			return;
		}
	}

	odbh_buffer_free(buf);
	odb_close(desc);
}