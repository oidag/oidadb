#include "../edbd.h"
#include "../edbs_u.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"

#include "../edbs-jobs.h"
#include "../edbs.h"

#include <pthread.h>

#include <stdio.h>
#include <wait.h>

struct threadpayload {
	pthread_t thread;
	int name;
	edbs_handle_t *h;
	uint32_t *shouldclose;
	int bytes_to_write_to_buff;
};

void *gothread(void *v) {
	struct threadpayload *payload = v;
	edb_err terr; // cannot use normal err sense we're multi-thread'n
	int i = 0;
	while(1) {

		// select a job
		edbs_job_t job;
		if ((terr = edbs_jobselect(payload->h, &job, payload->name))) {
			if(terr == EDB_ECLOSED) {
				// host shut down.
				break;
			}
			test_error("executor: edbs_jobselect");
			return 0;
		}

		// read from the buffer
		int j;
		for (j = 0; j < payload->bytes_to_write_to_buff; j++) {
			uint8_t res;
			int count = 1;
			terr = edbs_jobread(job, &res, count);
			if(terr) {
				test_error("executor: edbs_jobread");
				return 0;
			}
			if (res != (uint8_t)j) {
				test_error("executor: bad bytes");
				return 0;
			}
		}

		// write to buffer.
		for (j = 0; j < payload->bytes_to_write_to_buff; j++) {
			uint8_t res = j;
			int count = 1;
			terr = edbs_jobwrite(job, &res, count);
			if(terr) {
				test_error("executor: edbs_jobwrite");
			}
		}
		edbs_jobclose(job);
		i++;
	}
	return 0;
}

int main(int argc, const char **argv) {

	////////////////////////////////////////////////////////////////////////////
	const int handles = 0; // if and only if 0 the current process will be
	                       // the handle.
	const int hostthreads = 1; // must be at least 1.
	const int job_buffq = 6;
	const int jobs_to_install_per_handle = 10;
	const int bytes_to_write_to_buff = 4096;

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
	test_mkdir();
	test_mkfile(argv[0]);
	odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return 1;
	}

	uint32_t shouldclose;
	struct threadpayload payloads[hostthreads];

	// start processes
	int parentpid = getpid();
	int isparent = 1;
	for(int i =0; i < handles; i++) {
		isparent = fork();
		if(!isparent) break;
	}

	// if we're the parent: host the shm
	edbs_handle_t *host;
	if(isparent) {
		odb_hostconfig_t config = odb_hostconfig_default;
		config.job_buffq = job_buffq;
		if((err = edbs_host_init(&host, config))) {
			test_error("edbs_host_init");
			return 1;
		}

		// start threads
		for(int i = 0; i < hostthreads; i++) {
			payloads[i].h = host;
			payloads[i].bytes_to_write_to_buff = bytes_to_write_to_buff;
			payloads[i].shouldclose = &shouldclose;
			pthread_create(&payloads[i].thread, 0, gothread, &payloads[i]);
		}
	}

	// let the chidren install jobs and let the parent accept them
	// in multi-threaded style
	if(!isparent || handles == 0) {
		edbs_handle_t *hndl;
		if((err = edbs_handle_init(&hndl, parentpid))) {
			test_error("edbs_handle_init");
			return 1;
		}

		// install a bunch of jobs
		for(int i = 0; i < jobs_to_install_per_handle; i++) {

			// every 7th job will be a 1-way.
			int oneway = i % 7 == 0;

			edbs_job_t job;
			edbs_jobinstall(hndl, 69, getpid(), &job);
			if(oneway) {
				edbs_jobterm(job);
			}

			// fill up the transfer job
			// this is truely chaos sense we're writting 1 byte at a time.
			for( int j = 0; j < bytes_to_write_to_buff; j++) {
				int count = 1;
				uint8_t b = j;
				err = edbs_jobwrite(job, &b, count);
				if(err) {
					test_error("handle: edbs_jobwrite");
					break;
				}
			}
			// now read 1 byte at a time
			for( int j = 0; j < bytes_to_write_to_buff; j++) {
				int count = 1;
				uint8_t b = 0;
				err = edbs_jobread(job, &b, count);
				if(err) {
					if(oneway && err == EDB_ECLOSED) {
						// if its a 1-way, then we actually expect to get an error
						continue;
					} else {
						test_error("handle: edbs_jobread");
						break;
					}
				} else if(oneway) {
					test_error("expected to get error on jobread for 1-way");
					continue;
				}
				if(b !=(uint8_t) j) {
					test_error("did not read 98 from host");
				}
			}
		}
		edbs_handle_free(hndl);
		if(!isparent) {
			return test_waserror;
		}
	}

	// atp: host only

	// join child procs
	for(int i =0; i < handles; i++) {
		int pret;
		wait(&pret);
		if(pret) {
			test_error("child had bad bad exit code %d", pret);
		}
	}

	// close host, this will cause all children to break out of their loops.
	edbs_host_close(host);

	// join
	for(int i = 0; i < hostthreads; i++) {
		pthread_join(payloads[i].thread, 0);
	}

	edbs_host_free(host);


	// atp: only parent
	return test_waserror;


}