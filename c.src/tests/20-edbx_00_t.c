
#include <stdio.h>
#include <pthread.h>
#include <wait.h>
#include "teststuff.h"
#include "../include/oidadb.h"
#include "../wrappers.h"


struct threadpayload {
	pthread_t thread;
};

static struct shmobj {
} shmobj;

// forward declarations
pthread_t hostthread;
uint32_t host_futex = 0;
void *func_hostthread(void *v);
void handlethread();

////////////////////////////////////////////////////////////////////////////////
static const int handle_procs = 0; // how many processes to start (0 means none)
static const int handle_threads = 0; // how many threads to start (0 means none)

void test_main() {
	test_log("creating oidadb file...");
	struct odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return;
	}

	test_log("starting host thread...");
	pthread_create(&hostthread, 0, func_hostthread, 0);

	test_log("waiting for host bootup...");
	futex_wait(&host_futex, 0);
	switch (host_futex) {
		default:
		case ODB_VERROR:
			test_error("host futext fed error");
			return;
		case ODB_VACTIVE:
			test_log("...done.");
			break;
	}

	// atp: Our host thread is going strong. now time to create handles.

	test_log("launching %d handle processes...", handle_procs);
	for(int i = 0; i < handle_procs; i++) {
		// todo...
	}
	test_log("launching %d handle threads...", handle_threads);
	for(int i = 0; i < handle_threads; i++) {
		// todo...
	}

	test_log("waiting for processes to join...");
	for(int i = 0; i < handle_procs; i++) {
		// todo...
	}
	test_log("waiting for threads to join...");
	for(int i = 0; i < handle_threads; i++) {
		// todo...
	}

	test_log("closing host...");
	err = odb_hoststop();
	if(err) {
		test_error("failed to close host");
		return;
	}
	pthread_join(hostthread, 0);
	test_log("...closed.");
	return;
}

void *func_hostthread(void *v) {
	struct odb_hostconfig config = odb_hostconfig_default;
	config.stat_futex = &host_futex;
	odb_err err1 = odb_host(test_filenmae, config);
	if(err1) {
		test_error("odb_host returned error %d", err1);
	}
	return 0;
}