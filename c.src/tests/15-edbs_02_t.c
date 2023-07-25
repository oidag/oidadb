#define _GNU_SOURCE

#include "../edbd.h"
#include "../edbs_u.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"
#include "../wrappers.h"

#include "../edbs-jobs.h"
#include "../edbs.h"

#include <pthread.h>

#include <stdio.h>
#include <wait.h>
#include <sys/mman.h>
#include <stdatomic.h>
#include <malloc.h>


struct threadpayload {
	pthread_t thread;
	int name;
	edbs_handle_t *h;
	uint32_t *shouldclose;
	int bytes_to_write_to_buff_per, bytes_to_write_to_buff_mul;
	uint64_t *totaltimewriting;
	uint64_t *totaltimereading;
};

struct shmobj {
	uint32_t futex_ready; // set to 1 then broadcasted when host is ready for
	// child proces
	// to connect
};
struct shmobj *shmobj;

void test_main() {

	////////////////////////////////////////////////////////////////////////////
	const int handles = 1; // if and only if 0 the current process will be
	// the handle.
	const int hostthreads = 1; // must be at least 1.
	const int job_buffq = 1000;
	const int jobs_to_install_per_handle = 1;
	const int bytes_to_write_to_buff_per = 69;
	const int bytes_to_write_to_buff_mul = 1;

	// so the handles will install a bunch of jobs, and write
	// bytes_to_write_to_buff of the number '12' to the buffer, then read
	// bytes_to_write_to_buff bytes of the buffer expecting each byte to be
	// '98'.
	// The host will then thus launch hostthreads which will all go through
	// and read bytes_to_write_to_buff bytes of '12' and write back
	// bytes_to_write_to_buff bytes of '98'.
	// Once all the handles has had their jobs 'executed' the host will
	// detect that all handle processes have closed and proceed to shut down
	// the threads.


	// create an empty file
	struct odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return;
	}

	uint32_t shouldclose;
	struct threadpayload payloads[hostthreads];

	struct odb_hostconfig config = odb_hostconfig_default;
	config.job_buffq = job_buffq;

	edbs_handle_t *host;
	err = edbs_host_init(&host, config);
	if(err) {
		test_error("failed to init host");
		return;
	}

	// connect to self
	edbs_handle_t *handle;
	err = edbs_handle_init(&handle, getpid());
	if(err) {
		test_error("failed to connect to myself");
		return;
	}

	edbs_job_t hostjob, handlejob;
	err = edbs_jobinstall(handle, 123, &handlejob);
	if(err) {
		test_error("failed to install job");
		return;
	}
	err = edbs_jobselect(host, &hostjob, 567);
	if(err) {
		test_error("failed to select job");
		return;
	}
	int a1, a2;
	uint64_t b1, b2;
	char c1[8], c2[8];
	a1 = 0x100;
	b1 = 0x010;
	memcpy(c1, "abcdefg", 8);
	err = edbs_jobwritev(handlejob
			, &a1, sizeof(a1)
			, &b1, sizeof(b1)
			, &c1, 8
			, 0);
	if(err) {
		test_error("failed to term job");
		return;
	}

	err = edbs_jobread(hostjob
			, &a2, sizeof(a2));
	if(err) {
		test_error("failed to term job");
		return;
	}
	err = edbs_jobreadv(hostjob
			, &b2, sizeof(b2)
			, &c2, 8
			, 0);
	if(err) {
		test_error("failed to term job");
		return;
	}
	if(a1 != a2) {
		test_error("sent/received mismatch: a");
		return;
	}
	if(b1 != b2) {
		test_error("sent/received mismatch: a");
		return;
	}
	if(strcmp(c1, c2)) {
		test_error("string mismatch");
		return;
	}


	err = edbs_jobterm(hostjob);
	if(err) {
		test_error("failed to term job");
		return;
	}

	edbs_jobclose(hostjob);

	// close host, this will cause all children to break out of their loops.
	edbs_host_close(host);

	edbs_handle_free(handle);


	// atp: host only

	edbs_host_free(host);


	// atp: only parent
	return;
}