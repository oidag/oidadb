#include "../edbl.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"


#include <stdio.h>

void test_main() {

	// create an empty file
	struct odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return ;
	}

	// open the file
	int fd = open(test_filenmae, O_RDWR);
	if(fd == -1) {
		test_error("bad fd");
		return ;
	}

	// edbd open
	edbd_t dfile;
	edbd_t *p_dfile = &dfile;
	edbd_config config = edbd_config_default;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if(err) {
		test_error("edbd_open failed");
		return;
	}

	// edbl init
	edbl_host_t *host;
	if((err = edbl_host_init(&host, p_dfile))) {
		test_error("edbl_host_init");
		return ;
	}

	// open 2 handles.
	edbl_handle_t *h1, *h2;
	if((err = edbl_handle_init(host, &h1))) {
		test_error("edbl_handle_init");
		return ;
	}
	if((err = edbl_handle_init(host, &h2))) {
		test_error("edbl_handle_init");
		return ;
	}

	// test EDBL_LREF0C
	edbl_lock lock = {
			.type = EDBL_LREF0C,
			.eid  = 1,
	};
	if((err = edbl_set(h1, EDBL_ASH, lock))) {
		test_error("edbl_set");
		return ;
	}
	if(edbl_test(h2, EDBL_AXL, lock) != ODB_EAGAIN) {
		test_error("EDBL_LREF0C did not conflict with existing.");
		return ;
	}
	if((err = edbl_set(h1, EDBL_ARELEASE, lock))) {
		test_error("edbl_set");
		return ;
	}
	if((err = edbl_test(h2, EDBL_AXL, lock))) {
		test_error("EDBL_LREF0C conflicted with a supposed non-existing lock");
		return ;
	}

	// test EDBL_LREF0C
	lock = (edbl_lock){
			.type = EDBL_LROW,
			.object_pid = 2, // not a valid object page but whatever.
			.page_ioffset = 69
	};
	if((err = edbl_set(h1, EDBL_ASH, lock))) {
		test_error("edbl_set 2 ");
		return ;
	}
	if(edbl_test(h2, EDBL_AXL, lock) != ODB_EAGAIN) {
		test_error("EDBL_LROW did not conflict with existing 2");
		return ;
	}
	if((err = edbl_set(h1, EDBL_ARELEASE, lock))) {
		test_error("edbl_set 2");
		return ;
	}
	if((err = edbl_test(h2, EDBL_AXL, lock))) {
		test_error("EDBL_LROW conflicted with a supposed non-existing lock 2");
		return ;
	}

	// close handles
	edbl_handle_free(h1);
	edbl_handle_free(h2);

	edbl_host_free(host);

	// close edbd
	edbd_close(p_dfile);

	//close file
	close(fd);
}