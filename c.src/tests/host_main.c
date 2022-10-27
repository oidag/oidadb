#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "../include/ellemdb.h"
#include "../edbp.h"
#include "../edbd.h"
#include <pthread.h>
#include "../edbh.h"

int main() {
	unlink(".tests/hosttest");
	edb_hostconfig_t config = {
			.worker_poolsize = 8,
			.event_buffersize = 16,
			.flags = EDB_HCREAT,
			.page_multiplier = 2,
			.maxstructurepages = 1,
			.job_buffersize = 16,
			.job_transfersize = 4096,
			.slot_count = 32
	};
	edb_err err = 0;
	err = edb_host(".tests/hosttest", config);
	if(err) goto end;

	end:
	if(err)
		printf("failed with error: %d\n", err);
	return err;
}