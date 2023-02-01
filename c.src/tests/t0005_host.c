#include "teststuff.h"
#include "../include/oidadb.h"
#include "../edbp.h"
#include "../edbd.h"
#include "../edbh.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

static void *closeme(void *_) {
	err = odb_hostselect(test_filenmae, ODB_EVENT_HOSTED, 0);
	if(err) {
		test_error("odb_hostselect");
		return 0;
	}
	err = odb_hoststop(test_filenmae);
	if(err) {
		if(err != EDB_ENOHOST)
			test_error("odb_hoststop returned non-0");
			return 0;
	}
	err = odb_hostselect(test_filenmae, ODB_EVENT_CLOSED, 0);
	if(err) {
		test_error("odb_hostselect 2");
		return 0;
	}
	return 0;
}

int t0005() {
	err = odb_create(test_filenmae, odb_createparams_defaults);
	if(err) {
		test_error("failed to create file");
		return test_waserror;
	}

	// start a pthread
	pthread_t thread;
	pthread_create(&thread, 0, closeme, 0);

	err = odb_host(test_filenmae, odb_hostconfig_default);
	if(err) {
		test_error("host returned non-0");
		return 1;
	}
	pthread_join(thread, 0);


	return test_waserror;
}

int main(int argc, const char **argv) {
	test_mkdir();
	test_mkfile(argv[0]);
	t0005();
	return test_waserror;
}