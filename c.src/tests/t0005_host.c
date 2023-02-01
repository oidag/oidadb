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

	int i;
	for( i = 0; i < 10; i++) {
		err = odb_hoststop(test_filenmae);
		if(err) {
			if(err != EDB_ENOHOST)
			test_error("odb_hoststop returned non-0 that wasn't EDB_ENOHOST");
			return 0;
		}
		usleep(500);
	}
	if(i == 10) {
		test_error("odb_hoststop tried 10 times to close host but the host "
				   "was never found");
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


	return test_waserror;
}

int main(int argc, const char **argv) {
	test_mkdir();
	test_mkfile(argv[0]);
	t0005();
	return test_waserror;
}