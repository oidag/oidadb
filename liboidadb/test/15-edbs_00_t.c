#include "../edbd.h"
#include "../edbs_u.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"

#include "../edbs-jobs.h"
#include "../edbs.h"

#include <stdio.h>

void test_main() {

	////////////////////////////////////////////////////////////////////////////
	const int job_buffq = 16;

	edbs_job_t jobbuff[job_buffq];

	// create an empty file
	struct odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return;
	}

	// host the shm
	edbs_handle_t *shm_host;
	struct odb_hostconfig config = odb_hostconfig_default;
	config.job_buffq = job_buffq;
	if((err = edbs_host_init(&shm_host, config))) {
		test_error("host_init");
		goto ret;
	}

	// get a handle on the shm
	test_log("opening shm...");
	edbs_handle_t *shm_handle;
	if((err = edbs_handle_init(&shm_handle, getpid()))) {
		test_error("handle_init");
		goto ret;
	}
	{
		// dummy values
		shm_host->jobv[0].jobid = 1234;
		if(shm_handle->jobv[0].jobid != 1234) {
			test_error("host and handle not pointing to same shm memory");
			goto ret;
		}
	}

	test_log("installing jobs...");
	for(int i = 0; i < job_buffq; i ++) {
		edbs_job_t j;
		err = edbs_jobinstall(shm_host, ODB_JWRITE, &j);
		if(err) {
			test_error("job install");
			goto ret;
		}
	}
	// look inside the shm and make sure we have the expected values.
	if(shm_host->head->newjobs != job_buffq) {
		test_error("newjobs not as expected");
		goto ret;
	}
	if(shm_host->head->emptyjobs != 0) {
		test_error("emptyjobs not as expected");
		goto ret;
	}

	// now accept all the jobs
	test_log("accepting jobs...");
	for(int i = 0; i < job_buffq; i++) {
		err = edbs_jobselect(shm_handle, &jobbuff[i], i+100);
		if(err) {
			test_error("edbs_jobselect");
			goto ret;
		}
	}
	// check the shm for expected values
	test_log("checking expected values...");
	if(shm_host->head->newjobs != 0) {
		test_error("newjobs not as expected after accepting all jobs");
		goto ret;
	}

	// finally, close them.
	test_log("closing values...");
	for(int i = 0; i < job_buffq; i++) {
		edbs_jobclose(jobbuff[i]);
	}
	// check the shm for expected values
	test_log("expected values...");
	if(shm_host->head->emptyjobs != job_buffq) {
		test_error("newjobs not as expected after accepting all jobs");
		goto ret;
	}


	// free the handle
	ret:
	test_log("freeing handles...");
	edbs_handle_free(shm_handle);
	test_log("closing host...");
	edbs_host_close(shm_host);
	test_log("freeing host...");
	edbs_host_free(shm_host);

	test_log("done.");
}