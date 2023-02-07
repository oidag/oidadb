#include "../include/oidadb.h"
#include "../include/telemetry.h"
#include "teststuff.h"
#include "../edbp.h"
#include "../edbd.h"
#include "../edbh.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct {
	edbphandle_t *h;
	int pagec;
	edb_pid *pagev;
	int tests;
}threadstruct;

// returns null on success.
void *gothread(void *args) {
	threadstruct *t = args;
	edbphandle_t *h = t->h;
	for(int i = 0; i < t->pagec; i++) {
		err = edbp_start(h, t->pagev[i]);
		if (err) {
			test_error("edbp_start");
			continue;
		}
		edbp_finish(h);
	}
	return (void *)test_waserror;
}

int main(int argc, const char **argv) {

	// create an empty file
	test_mkdir();
	test_mkfile(argv[0]);
	odb_createparams createparams  =odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if(err) {
		test_error("failed to create file");
		return 1;
	}
	// open the file
	int fd = open(test_filenmae, O_RDWR);
	if(fd == -1) {
		test_error("bad fd");
		return 1;
	}
	odbtelem(1);
	//odbtelem_bind(ODBTELEM_PAGES_NEWDEL, newdel);
	edbd_t dfile;
	edbd_config config;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if(err) {
		test_error("edbd_open failed");
		return 1;
	}


	// testing configs
	const int pagec = 64; // pages to create
	const int page_strait = 1; // strait of pages
	const int cachesize = 8; // pra cache
	const int threads = 1; // threads to start
	const int threads_tests = 64; // page count each thread should test
	// if a pageid is divisable by this, that means the edbp handles will
	// avoid brining it into cache, thus testing the PRU algo.
	const int avoiddivisor = 10;
	int rand_seed = 4844; // use same random seed for static results.
	/*time_t tv;
	int rand_seed = (unsigned) time(&tv) + getpid();*/

	// working vars.
	edb_pid pages[pagec];
	pthread_t threadv[threads];
	edbphandle_t handle[threads];
	threadstruct t[threads];
	edb_pid pageidorder[threads_tests * threads];

	// create pages
	for(int i = 0; i < pagec; i++) {
		err = edbd_add(&dfile, page_strait, &pages[i]);
		if(err) {
			test_error("edbd_add 1");
			goto ret;
		}
	}

	// init the cache
	edbpcache_t cache;
	err = edbp_init(&cache, &dfile, cachesize);
	if(err) {
		test_error("edbp_init");
		goto ret;
	}

	// create thread list
	srand(rand_seed);
	for(int i =0; i < threads_tests * threads; i++) {
		pageidorder[i] = (edb_pid) (rand() % (pagec-1))+1;
	}


	// create handles
	for(int i = 0;i  < threads; i++) {
		err = edbp_newhandle(&cache, &handle[i], i);
		if(err) {
			test_error("new handle");
			goto ret;
		}
		threadstruct *tr = &t[i];
		tr->pagec = threads_tests;
		tr->pagev = pageidorder + (threads_tests*i);
		tr->h = &handle[i];
		tr->tests = threads_tests;
		pthread_create(&threadv[i], 0, gothread, tr);
	}

	// join threads
	for(int i = 0;i  < threads; i++) {
		void *ret;
		pthread_join(threadv[i], &ret);
		if(ret) {
			test_error("pthread return");
			goto ret;
		}
	}

	// todo: print out results.



	for(int i = 0;i  < threads; i++) {
		edbp_freehandle(&handle[i]);
	}


	edbp_decom(&cache);
	edbd_close(&dfile);

	ret:
	return test_waserror;

	/*
	edbd_t file;
	edbpcache_t cache;
	edb_err err = 0;
	unlink(".tests/page_test");
	err = edbd_open(&file, ".tests/page_test", 1, EDB_HCREAT);
	if(err) {
		goto ret;
	}
	err = edbp_init(&cache, &file, 16);
	if(err) {
		goto fclose;
	}

	pthread_t threads[8];
	for(int i = 0; i < 8; i++) {
		pthread_create(&threads[i], 0, gothread, &cache);
	}

	for(int i = 0; i < 8; i++) {
		pthread_join(threads[i], 0);
	}

	decom:
	edbp_decom(&cache);
	fclose:
	edbd_close(&file);
	ret:
	if(err) {
		if(err == EDB_EERRNO) {
			perror("edbp errorno");
		}
		printf("edb error: %d (errno: %d)\n", err, errno);
		return 1;
	}
	return 0;*/
}