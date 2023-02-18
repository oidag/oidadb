#include "../edbd.h"
#include "../edbs_u.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"

#include "../edbs-jobs.h"
#include "../edbs.h"

#include <stdio.h>

int main(int argc, const char **argv) {

	////////////////////////////////////////////////////////////////////////////
	const int job_buffq = 16;

	edbs_job_t jobbuff[job_buffq];

	// create an empty file
	test_mkdir();
	test_mkfile(argv[0]);
	odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return 1;
	}

	// host the shm
	edbs_handle_t *shm_host;
	odb_hostconfig_t config = odb_hostconfig_default;
	config.job_buffq = job_buffq;
	if((err = edbs_host_init(&shm_host, config))) {
		test_error("host_init");
		goto ret;
	}

	// get a handle on the shm
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

	for(int i = 0; i < job_buffq; i ++) {
		edbs_job_t j;
		err = edbs_jobinstall(shm_host, 69, 420, &j);
		if(err) {
			test_error("job install");
			goto ret;
		}
	}
	// look inside the shm and make sure we have the expected values.
	if(shm_host->head->futex_newjobs != job_buffq) {
		test_error("futex_newjobs not as expected");
		goto ret;
	}
	if(shm_host->head->futex_emptyjobs != 0) {
		test_error("futex_emptyjobs not as expected");
		goto ret;
	}

	// now accept all the jobs
	for(int i = 0; i < job_buffq; i++) {
		err = edbs_jobselect(shm_handle, &jobbuff[i], i+100);
		if(err) {
			test_error("edbs_jobselect");
			goto ret;
		}
	}
	// check the shm for expected values
	if(shm_host->head->futex_newjobs != 0) {
		test_error("futex_newjobs not as expected after accepting all jobs");
		goto ret;
	}

	// finally, close them.
	for(int i = 0; i < job_buffq; i++) {
		edbs_jobclose(jobbuff[i]);
	}
	// check the shm for expected values
	if(shm_host->head->futex_emptyjobs != job_buffq) {
		test_error("futex_newjobs not as expected after accepting all jobs");
		goto ret;
	}


	// free the handle
	ret:
	edbs_handle_free(shm_handle);
	edbs_host_free(shm_host);

	return test_waserror;
}