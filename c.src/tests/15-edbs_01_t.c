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
#include <sys/mman.h>
#include <stdatomic.h>

struct threadpayload {
	pthread_t thread;
	int name;
	edbs_handle_t *h;
	uint32_t *shouldclose;
	int bytes_to_write_to_buff;
	uint64_t *totaltimewriting;
	uint64_t *totaltimereading;
};

struct shmobj {
	uint32_t futex_ready; // set to 1 then broadcasted when host is ready for
	// child proces
	// to connect
};
struct shmobj *shmobj;

const char *shmname = "/10EDBS_00_01_c";

void *gothread(void *v) {
	struct threadpayload *payload = v;
	edb_err terr; // cannot use normal err sense we're multi-thread'n
	int i = 0;
	uint64_t start = 0,finish = 0,diff = 0;
	struct timeval startv,end = {0};
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

		edbs_shmjob_t *debugjob = &job.shm->jobv[job.jobpos];

		/*printf("thread#%d adopted job#%ld (slot %d)\n", payload->name,
			   debugjob->jobid,
			   job.jobpos);*/

		// read from the buffer
		int j;
		gettimeofday(&startv,0);
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
		gettimeofday(&end,0);
		start = (uint64_t)startv.tv_sec*1000000 + startv.tv_usec;
		finish = (uint64_t)end.tv_sec*1000000 + end.tv_usec;
		diff = finish - start;
		atomic_fetch_add(payload->totaltimereading, diff);

		// write to buffer.
		gettimeofday(&startv,0);
		for (j = 0; j < payload->bytes_to_write_to_buff; j++) {
			uint8_t res = j;
			int count = 1;
			terr = edbs_jobwrite(job, &res, count);
			if(terr) {
				test_error("executor: edbs_jobwrite");
			}
		}
		gettimeofday(&end,0);
		edbs_jobclose(job);

		start = (uint64_t)startv.tv_sec*1000000 + startv.tv_usec;
		finish = (uint64_t)end.tv_sec*1000000 + end.tv_usec;
		diff = finish - start;
		atomic_fetch_add(payload->totaltimewriting, diff);

		i++;
	}
	return 0;
}

int main(int argc, const char **argv) {

	////////////////////////////////////////////////////////////////////////////
	const int handles = 6; // if and only if 0 the current process will be
	                       // the handle.
	const int hostthreads = 6; // must be at least 1.
	const int job_buffq = 100;
	const int jobs_to_install_per_handle = 100;
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

	{
		// initialize shm object FOR THE PARENT PROC
		int shmfd = shm_open(shmname, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (shmfd == -1) {
			test_error("shm open");
			return 1;
		}
		ftruncate64(shmfd, 4096);
		shmobj = mmap(0, sizeof(struct shmobj),
		              PROT_READ | PROT_WRITE,
		              MAP_SHARED, shmfd, 0);
		if (shmobj == MAP_FAILED) {
			test_error("mmap");
			return 1;
		}
		close(shmfd);
		madvise(shmobj, 4096, MADV_DONTFORK);
		shmobj->futex_ready = 0;
	}

	// start processes
	int parentpid = getpid();
	int isparent = 1;
	for(int i =0; i < handles; i++) {
		isparent = fork();
		if(!isparent) break;
	}
	int mypid = getpid();

	// if we're the parent: host the shm
	edbs_handle_t *host;
	uint64_t totaltimereading = 0, totaltimewritting = 0;
	if(isparent) {
		odb_hostconfig_t config = odb_hostconfig_default;
		config.job_buffq = job_buffq;
		err = edbs_host_init(&host, config);
		shmobj->futex_ready = 1;
		futex_wake(&shmobj->futex_ready, INT32_MAX);
		if(err) {
			test_error("edbs_host_init");
			return 1;
		}

		// start threads
		for(int i = 0; i < hostthreads; i++) {
			payloads[i].h = host;
			payloads[i].totaltimereading = &totaltimereading;
			payloads[i].totaltimewriting = &totaltimewritting;
			payloads[i].name = i+1;
			payloads[i].bytes_to_write_to_buff = bytes_to_write_to_buff;
			payloads[i].shouldclose = &shouldclose;
			pthread_create(&payloads[i].thread, 0, gothread, &payloads[i]);
		}
	} else {
		// child proc initialize shmobject
		int shmfd = shm_open(shmname, O_RDWR, 0666);
		if(shmfd == -1) {
			test_error("shm open 2");
			return 1;
		}
		shmobj = mmap(0, sizeof(struct shmobj),
		              PROT_READ | PROT_WRITE,
		              MAP_SHARED, shmfd, 0);
		if(shmobj == MAP_FAILED) {
			test_error("mmap 2");
			return 1;
		}
		close(shmfd);
	}

	// let the chidren install jobs and let the parent accept them
	// in multi-threaded style
	if(!isparent || handles == 0) {
		if(!isparent) {
			futex_wait(&shmobj->futex_ready, 0);
		}
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
			printf("proc %d finished \n", mypid);
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

	// results
	printf("total time reading (seconds): %f\n",
	       (double)totaltimereading/10000000);
	printf("total time writing (seconds): %f\n",
		   (double)totaltimewritting/10000000);


	// atp: only parent
	return test_waserror;


}