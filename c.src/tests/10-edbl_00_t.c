#include "../edbl.h"
#include "../include/telemetry.h"
#include "../include/oidadb.h"
#include "teststuff.h"


#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <wait.h>
#include <sys/mman.h>
#include <sys/file.h>

typedef enum {
	LOCKTYPE_EDBL,
	LOCKTYPE_FCNTL,
	LOCKTYPE_MUTEX,
} locktype;

struct threadpayload {
	pthread_t pthread;
	const char *file;
	int bytes;
	int blen;
	int testc;
	unsigned long timespent;
	pthread_mutex_t *mutex;
	edbl_handle_t *edbl;
	locktype type;
};

struct shmobj {
	atomic_int_least64_t totaltime;
	pthread_mutex_t shmmutex;
	int www;
};

struct shmobj *shmobj;

int fcntl_lock(int fd, struct flock64 f) {
	return fcntl(fd,F_OFD_SETLKW,&f);
}
int fcntl_unlock(int fd, struct flock64 f) {
	return fcntl(fd,F_OFD_SETLKW,&f);
}

static void *gothread(void *a) {
	struct threadpayload *payload = a;

	int fd = open(payload->file, O_RDWR);
	if(fd == -1) {
		test_error("open");
		return 0;
	}

	struct timeval start,end = {0};
	for(int i = 0; i < payload->testc; i++) {
		int bytetolock = rand() % payload->bytes;
		int bytelen = 1;
		struct flock64 flk = {0};
		flk.l_len = bytelen;
		flk.l_start = bytetolock;
		flk.l_whence = SEEK_SET;
		flk.l_type = F_WRLCK;
		flk.l_pid = 0;
		edbl_lock ref;
		ref.type = EDBL_LARBITRARY;
		ref.l_len = 1;
		ref.l_start = bytetolock;

		gettimeofday(&start,0);
		switch (payload->type) {
			case LOCKTYPE_EDBL:
				edbl_set(payload->edbl, EDBL_AXL, ref);
				break;
			case LOCKTYPE_FCNTL:
				fcntl_lock(fd, flk);
				break;
			case LOCKTYPE_MUTEX:
				pthread_mutex_lock(payload->mutex);
				break;
		}
		gettimeofday(&end,0);
		uint8_t w, w2;
		lseek(fd, bytetolock, SEEK_SET);
		ssize_t ret = read(fd, &w, 1);
		if(ret == -1) {
			test_error("failed to read byte");
		}
		lseek(fd, bytetolock, SEEK_SET);
		w++;
		ret = write(fd, &w, 1);
		if(ret == -1) {
			test_error("failed to write byte");
		}

		uint64_t startt = (uint64_t)start.tv_sec*1000000 + start.tv_usec;
		uint64_t finisht = (uint64_t)end.tv_sec*1000000 + end.tv_usec;
		unsigned long diff = finisht - startt;
		payload->timespent += diff;
		lseek(fd, bytetolock, SEEK_SET);
		read(fd, &w2, 1);
		if(w != w2) {
			test_error("lock failed.");
		}

		switch (payload->type) {
			case LOCKTYPE_EDBL:
				edbl_set(payload->edbl, EDBL_ARELEASE, ref);
				break;
			case LOCKTYPE_FCNTL:
				flk.l_type = F_UNLCK;
				fcntl_unlock(fd, flk);
				break;
			case LOCKTYPE_MUTEX:
				pthread_mutex_unlock(payload->mutex);
				break;
		}
	}

	close(fd);



	// set it up and lets test the speed between mutexes, futexes, OFD locks
	// and switch to mutli-process and give normal locks a try.
}

int main(int argc, const char **argv) {

	test_mkdir();
	test_mkfile(argv[0]);

	creat64(test_filenmae, 0666);

	////////////////////////////////////////////////////////////////////////////
	//gunna need multi threads
	const int extrathreads = 11;
	const int extraprocesses = 0;
	const int bytes = 100;
	const locktype locktypetouse = LOCKTYPE_EDBL;
	const int testc = 50000;

	// working vars
	struct threadpayload threads[extrathreads + 1];
	pid_t procs[extraprocesses+1];
	int ischild = 0;

	// truncate file
	if(truncate64(test_filenmae, bytes) == -1) {
		test_error("truncate");
		goto ret;
	}

	// initialize shm object.
	const char *shmname = "/10EDBL_00_00_c";
	int shmfd = shm_open(shmname, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if(shmfd == -1) {
		test_error("shm open");
		goto ret;
	}
	ftruncate64(shmfd, 4096);
	shmobj = mmap(0, sizeof(struct shmobj),
	              PROT_READ | PROT_WRITE,
	              MAP_SHARED, shmfd, 0);
	if(shmobj == MAP_FAILED) {
		test_error("mmap");
		if(ischild) return 1;
		goto ret;
	}
	close(shmfd);
	madvise(shmobj, 4096, MADV_DONTFORK);

	{
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&shmobj->shmmutex, &attr);
	}


	procs[extraprocesses] = getpid();

	// start extra processes
	for(int i = 0; i < extraprocesses; i++) {
		pid_t t = fork();
		if(!t) {
			// child
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
				goto ret;
			}
			close(shmfd);
			ischild = 1;
			break;
		} else {
			// parent
			procs[i] = t;
		}
	}

	// initialize paylaods
	edbl_host_t *handle;
	// cheese host_init a little bit by only supplying the only thing that
	// matters: the file descriptor.
	edbd_t f;
	f.descriptor = open(test_filenmae, O_RDWR);
	if(f.descriptor == -1) {
		test_error("open(2)");
		return -1;
	}
	err = edbl_host_init(&handle, &f);
	if(err) {
		test_error("edbl_host_init");
		goto procfail;
	}
	for(int i = 0;i < extrathreads+1; i++) {
		err = edbl_handle_init(handle, &threads[i].edbl);
		if(err) {
			test_error("edbl_handle_init");
			goto procfail;
		}
		threads[i].file = test_filenmae;
		threads[i].bytes = bytes;
		threads[i].testc = testc;
		threads[i].timespent = 0;
		threads[i].mutex = &shmobj->shmmutex;
		threads[i].type = locktypetouse;
	}
	close(f.descriptor);



	// start extra threads
	for(int i = 0; i < extrathreads; i++) {
		pthread_create(&threads[i].pthread, 0, gothread, &threads[i]);
	}
	// start the host-thread
	gothread(&threads[extrathreads]);

	// join threads
	for(int i = 0; i < extrathreads; i++) {
		pthread_join(threads[i].pthread, 0);
		shmobj->totaltime += threads[i].timespent;
		edbl_handle_free(threads[i].edbl);
		printf("process%d thread %d: %ld\n", getpid(), i,
		       threads[i].timespent);
	}
	edbl_handle_free(threads[extrathreads].edbl);
	shmobj->totaltime += threads[extrathreads].timespent;
	printf("process%d thread %d: %ld\n", getpid(), extrathreads,
		   threads[extrathreads].timespent);

	procfail:
	edbl_host_free(handle);
	if(ischild) {
		shm_unlink(shmname);
		return test_waserror;
	}

	// join processes
	for(int i = 0; i < extraprocesses; i++) {
		int pret;
		wait(&pret);
		if(pret) {
			test_error("child had bad bad exit code %d", pret);
		}
	}

	printf("Lock total time (seconds): %f\n", (double)shmobj->totaltime/10000000);
	printf("Avg lock time (usec): %f\n", (double)
			                                     shmobj->totaltime/
			(((extraprocesses+1) * (extrathreads+1)) * testc));

	shm_unlink(shmname);

	ret:
	return test_waserror;
}