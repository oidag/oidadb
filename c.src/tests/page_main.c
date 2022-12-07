#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "../include/ellemdb.h"
#include "../edbp.h"
#include "../edbd.h"
#include <pthread.h>
#include "../edbh.h"


void *gothread(void *args) {
	edbpcache_t *cache = args;
	edbphandle_t handle;
	edb_err err;
	err = edbp_newhandle(cache, &handle);
	if(err) {
		perror("failed to create handle");
		return 0;
	}

	// create 256 new pages.
	for(int i = 0; i < 256; i++) {
		err = createapage(&handle);
		if(err) {
			perror("createpage error.");
			goto freehandle;
		}

	}

	// test the page replacement algo.
	// access the pages *somewhat* randomly.
	//
	// start with a random number between 1 to 255.
	//
	// if the number is divisable by 3, then load
	for(int i = 0; i < 256; i++) {
		edb_pid ir = rand() % 255 + 1;
		edbp_start(&handle, ir);
		edbp_t mypage = edbp_graw(&handle);
		char *mybody = mypage.bodyv;
		mybody[i] = (char)i;
		edbp_mod(&handle, EDBP_CACHEHINT, EDBP_HDIRTY);
		edbp_finish(&handle);
	}

	freehandle:
	edbp_freehandle(&handle);
}

int main() {
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
	return 0;
}