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

struct threadpayload {
	pthread_t pthread;
	const char *file;
	int bytes;
	int blen;
	int testc;
	unsigned long timespent;
	pthread_mutex_t *mutex;
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
		int bytetolock = 0;//rand() % payload->bytes;
		int bytelen = 1;
		struct flock64 flk = {0};
		flk.l_len = bytelen;
		flk.l_start = bytetolock;
		flk.l_whence = SEEK_SET;
		flk.l_type = F_WRLCK;
		flk.l_pid = 0;

		gettimeofday(&start,0);
		int r = fcntl_lock(fd, flk);
		//pthread_mutex_lock(payload->mutex);
		//flock(fd, LOCK_EX);
		gettimeofday(&end,0);
		int w = shmobj->www;
		shmobj->www++;

		uint64_t startt = (uint64_t)start.tv_sec*1000000 + start.tv_usec;
		uint64_t finisht = (uint64_t)end.tv_sec*1000000 + end.tv_usec;
		unsigned long diff = finisht - startt;
		payload->timespent += diff;
		usleep(10);
		if(w+1 != shmobj->www) {
			test_error("nope");
		}

		flk.l_type = F_UNLCK;
		fcntl_unlock(fd, flk);
		//pthread_mutex_unlock(payload->mutex);
		//flock(fd, LOCK_UN);
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
	const int extrathreads = 3;
	const int extraprocesses = 2;
	const int bytes = 1;
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
	int shmfd = shm_open(shmname, O_RDWR | O_CREAT | O_EXCL, 0666);
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
	for(int i = 0;i < extrathreads+1; i++) {
		threads[i].file = test_filenmae;
		threads[i].bytes = bytes;
		threads[i].testc = testc;
		threads[i].timespent = 0;
		threads[i].mutex = &shmobj->shmmutex;
	}



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
		printf("process%d thread %d: %ld\n", getpid(), i,
		       threads[i].timespent);
	}
	shmobj->totaltime += threads[extrathreads].timespent;
	printf("process%d thread %d: %ld\n", getpid(), extrathreads,
		   threads[extrathreads].timespent);

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