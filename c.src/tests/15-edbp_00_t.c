#define _GNU_SROUCE

#include "../options.h"
#include "../include/oidadb.h"
#include "../include/telemetry.h"
#include "teststuff.h"
#include "../edbp.h"
#include "../edbd.h"
#include "../edbh.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdatomic.h>

atomic_int *page_loaded_amount;
atomic_int *page_cached_amount;

// total time inside of edbp_start (microseconds)
unsigned long totalspent = 0;

typedef struct {
	edbphandle_t *h;
	int pagec;
	odb_pid *pagev;
	int tests;
}threadstruct;

void pload(struct odbtelem_data d) {
	switch(d.class) {
		case ODBTELEM_WORKR_PLOAD:
			page_loaded_amount[d.pageid-65]++;
			break;
		case ODBTELEM_PAGES_CACHED:
			page_cached_amount[d.pageid-65]++;
			break;
	}
}

// returns null on success.
void *gothread(void *args) {
	threadstruct *t = args;
	edbphandle_t *h = t->h;
	struct timeval start,end = {0};
	for(int i = 0; i < t->pagec; i++) {
		gettimeofday(&start, 0);
		err = edbp_start(h, t->pagev[i]);
		odb_pid pageid = t->pagev[i];
		gettimeofday(&end, 0);
		if (err) {
			test_error("edbp_start");
			continue;
		}
		// dirty the page for worst case-senario.
		unsigned int *page = edbp_graw(h) + ODB_SPEC_HEADSIZE;
		page[0x0]++;
		page[0x1] = 0x69697777;
		edbp_mod(h, EDBP_CACHEHINT, EDBP_HDIRTY);
		unsigned long int startt, finisht;
		startt = (uint64_t)start.tv_sec*1000000 + start.tv_usec;
		finisht = (uint64_t)end.tv_sec*1000000 + end.tv_usec;
		unsigned long diff = finisht - startt;
		atomic_fetch_add(&totalspent, diff);
		edbp_finish(h);
	}
	return (void *)test_waserror;
}

void test_main() {
	// create an empty file
	struct odb_createparams createparams  =odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if(err) {
		test_error("failed to create file");
		return;
	}
	// open the file
	int fd = open(test_filenmae, O_RDWR
								 | O_SYNC
								 | O_NONBLOCK);
	if(fd == -1) {
		test_error("bad fd");
		return;
	}
	odbtelem(1, (struct odbtelem_params){.buffersize_exp=5});
	odbtelem_bind(ODBTELEM_WORKR_PLOAD, pload);
	odbtelem_bind(ODBTELEM_PAGES_CACHED, pload);
	edbd_t dfile;
	edbd_config config;
	config.delpagewindowsize = 1;
	err = edbd_open(&dfile, fd, config);
	if(err) {
		test_error("edbd_open failed");
		return;
	}

	////////////////////////////////////////////////////////////////////////////
	//
	// testing configs. Play with these for science.
	//
	////////////////////////////////////////////////////////////////////////////
	const int pagec = 15000; // pages to create
	const int page_strait = 1; // strait of pages
	const int cachesize = 256; // pra cache
	const int threads = 16; // threads to start
	const int threads_tests = 100; // page count each thread should test
	// I'm not sure what this means. But I try to make this a percent.
	//
	// When I set it to 0.7 this makes 30% of the pages loaded 70% of the time.
	// I think.... at least thats what I'm going for.
	//
	// Check my work.
	const double algothingindexthing = 0.6;

	int rand_seed = 4844; // use same random seed for static results.
	/*time_t tv;
	int rand_seed = (unsigned) time(&tv) + getpid();*/

	// working vars.
	odb_pid pages[pagec];
	pthread_t threadv[threads];
	edbphandle_t *handle[threads];
	threadstruct t[threads];
	odb_pid pageidorder[threads_tests * threads];
	page_loaded_amount = malloc(sizeof(atomic_int) * pagec);
	page_cached_amount = malloc(sizeof(atomic_int) * pagec);
	bzero(page_loaded_amount, sizeof(atomic_int) * pagec);
	bzero(page_cached_amount, sizeof(atomic_int) * pagec);

	// create pages
	for(int i = 0; i < pagec; i++) {
		err = edbd_add(&dfile, page_strait, &pages[i]);
		if(err) {
			test_error("edbd_add 1");
			goto ret;
		}
	}

	// init the cache
	edbpcache_t *cache;
	err = edbp_cache_init(&dfile, &cache);
	if(err) {
		test_error("edbp_cache_init");
		goto ret;
	}
	err = edbp_cache_config(cache, EDBP_CONFIG_CACHESIZE, cachesize);
	if(err) {
		test_error("edbp_cache_config");
		goto ret;
	}

	// create page list: the full list of all pages that will be loaded in
	// which order (save for multithreading). Here is where you apply
	// preferences to pages.
	srand(rand_seed);
	int maxindex = pagec;
	int listsize = threads_tests * threads;
	int listnval = 0;
	for(int i =0; i < listsize; i++) {
		if(listnval == i) {
			maxindex = (int)((double)maxindex * (1-algothingindexthing))+1;
			listnval = (int)((1-algothingindexthing) * (double)(listsize-i))+i;
		}
		pageidorder[i] = pages[rand() % maxindex];

	}

	// create handles
	for(int i = 0;i  < threads; i++) {
		err = edbp_handle_init(cache, i, &handle[i]);
		if(err) {
			test_error("new handle");
			break;
		}
		threadstruct *tr = &t[i];
		tr->pagec = threads_tests;
		tr->pagev = &pageidorder[threads_tests*i];
		tr->h = handle[i];
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


	printf("results\n");
	int sumloads = 0;
	int sumfaults = 0;
	printf("| PID     | LOADED  | FAULT   |\n");
	printf("|---------|---------|---------|\n");
	for(int i = 0; i < pagec; i++) {
		printf("| %7ld | %7d | %7d |\n",
			   pages[i],
			   page_loaded_amount[i],
			   page_cached_amount[i]);
		sumloads += page_loaded_amount[i];
		sumfaults += page_cached_amount[i];
	}
	printf("sum loads: %d\n", sumloads);
	printf("sum faults: %d\n", sumfaults);
	printf("PRA Inefficiency: %f\n", (float)sumfaults / (float)sumloads);
	printf("PRA total time (seconds): %f\n", (double)totalspent/1000000);
	printf("PRA avg time per thread (seconds): %f\n", (double)
			(totalspent)/(double)threads/1000000);
	printf("PRA avg total time per call (usec): %f\n",
		   (double)(totalspent)/(double)sumloads);
	if(sumloads != threads * threads_tests) {
		test_error("total loads: %d (%d expected)", sumloads, threads *
		threads_tests);
	}

	free(page_loaded_amount);
	free(page_cached_amount);
	for(int i = 0;i  < threads; i++) {
		edbp_handle_free(handle[i]);
	}


	edbp_cache_free(cache);
	edbd_close(&dfile);

	ret:
	return;
}