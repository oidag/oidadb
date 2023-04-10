#include "include/telemetry.h"
#include "options.h"
#include "telemetry.h"
#include "errors.h"
#include "wrappers.h"
#include "odb-structures.h"

#include <strings.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <memory.h>
#include <malloc.h>

static int telemenabled = 0;
odbtelem_params_t startedparams;

#ifdef EDBTELEM_DEBUG
static const char *class2str(odbtelem_class c) {
	switch (c) {
		case ODBTELEM_PAGES_NEWOBJ:
			return "ODBTELEM_PAGES_NEWOBJ";
	case ODBTELEM_PAGES_NEWDEL:
		return "ODBTELEM_PAGES_NEWDEL";
	case ODBTELEM_PAGES_CACHED:
		return "ODBTELEM_PAGES_CACHED";
	case ODBTELEM_PAGES_DECACHED:
		return "ODBTELEM_PAGES_DECACHED";
	case ODBTELEM_WORKR_ACCEPTED:
		return "ODBTELEM_WORKR_ACCEPTED";
	case ODBTELEM_WORKR_PLOAD:
		return "ODBTELEM_WORKR_PLOAD";
	case ODBTELEM_WORKR_PUNLOAD:
		return "ODBTELEM_WORKR_PUNLOAD";
	case ODBTELEM_JOBS_ADDED:
		return "ODBTELEM_JOBS_ADDED";
	case ODBTELEM_JOBS_COMPLETED:
		return "ODBTELEM_JOBS_COMPLETED";
		default:return "UNKNOWN";
	}
}
static void odb_data_process(odbtelem_data d) {
	log_debugf("%s(pid: %ld, eid/workid: %d, pagec/jobid: %d)",
			   class2str(d.class),
			   d.arg0,
			   d.arg1,
			   d.arg2);
	odbtelem_install(d);
}
#else
#define odb_data_process(d) odbtelem_install(d)
#endif

#define telemetry_shm_magicnum 0x7373FDFD

// note: will be read-only to lossly metrics. Need to be read/write mode so
// that lossless stuff will work.
typedef struct telemetry_shm {

	// total bytes of shm size.
	const int shmc;

	// buffersize_exp. Must be at least 2 due to how the lossless engineering is
	// set up.
	//
	// Will always be 2^x. Thus index will always equal futex_raster % dataq
	// even when futex_raster overflows UINT32_MAX
	const int dataq;

	// the index of newest item.
	// But more specifically, this will always equal raster % dataq.
	//
	// I don't see why you should ever use this in leu of keeping track of
	// only your local proc index.
	int index;

	// will increment by 1 for every single event and then the host will
	// broadcast.
	//
	// Will also increment by 1 when hosted goes from 1 to 0 (and thus you
	// should shutdown)
	//
	// on host side: wrap this incrmenet and installment inside of a mutex.
	uint32_t futex_raster;

	// set to 1 when all set up.
	// set to 0 when host is no longer tending to this shm.
	int hosted;

	// the current image by the host will be updated before every futex_raster.
	// All pointers in this will not be actual pointers! They instead will be
	// uint64_t offsets from the start of shm to where the data begins (sense
	// shm memory can have no pointers)
	//
	// image_raster, the raster id that the image reflects. image_raster is
	// only updated AFTER the image has been fully updated. futex_raster is
	// only updated BEFORE the image is updated. This will allow for a
	// brute-mutex.
	odbtelem_image_t image;
	uint32_t         image_raster;

	// we put this last due to how memcpy works as to know that so long as
	// this is valid, shmc is also valid. Old fashion way of mutli processing :)
	const int magicnum;

	// after this structure:
	//odbtelem_data *datav;

} telemetry_shm;

struct {
	uint32_t index;
	uint32_t raster;
	uint32_t attached;
} telemetry_listener = {0};

struct {
	pthread_mutex_t mutex;
} telemetry_host = {0};

struct {
	char           shm_name[32];
	telemetry_shm *shm;
	odbtelem_data *shm_datav;
} telemtry_shared = {0};

void static inline shmname(pid_t pid, char *buff) {
	sprintf(buff, "/odb_host_telem-%d", pid);
}


odb_err odbtelem_attach(const char *path) {
#ifndef EDBTELEM
	return ODB_EVERSION;
#endif
	if(telemetry_listener.attached) {
		return ODB_EOPEN;
	}
	odb_err err;
	pid_t hostpid;
	if((err = edb_host_getpid(path, &hostpid))) {
		return err;
	}
	shmname(hostpid, telemtry_shared.shm_name);
	int fd = shm_open(telemtry_shared.shm_name,
			 O_RDONLY,
			 0666);
	if(fd == -1) {
		switch (errno) {
			case ENOENT: return ODB_EPIPE;
			default: return ODB_ECRIT;
		}
	}

	// grab the heading
	telemetry_shm tmphead;
	ssize_t n = read(fd, &tmphead, sizeof(telemetry_shm));
	if(n == -1) {
		log_critf("read");
		close(fd);
		shm_unlink(telemtry_shared.shm_name);
		return ODB_ECRIT;
	}
	if(tmphead.magicnum != telemetry_shm_magicnum) {
		// not done initializing
		close(fd);
		shm_unlink(telemtry_shared.shm_name);
		return ODB_EPIPE;
	}

	// do the actual map
	telemtry_shared.shm = mmap(0, tmphead.shmc,
	                           PROT_READ,
	                           MAP_SHARED,
	                           fd, 0);
	close(fd);
	if(telemtry_shared.shm == MAP_FAILED) {
		log_critf("mmap");
		telemtry_shared.shm = 0;
		shm_unlink(telemtry_shared.shm_name);
		return ODB_ECRIT;
	}
	telemtry_shared.shm_datav = (void *)telemtry_shared.shm
			+ sizeof(telemetry_shm);
	telemetry_listener.raster = telemtry_shared.shm->futex_raster;
	telemetry_listener.index  = telemetry_listener.raster
			% telemtry_shared.shm->dataq;
	telemetry_listener.attached = 1;
	return 0;
}
void odbtelem_detach() {
	if(!telemtry_shared.shm) return;
	munmap(telemtry_shared.shm, telemtry_shared.shm->shmc);
	shm_unlink(telemtry_shared.shm_name);
	telemtry_shared.shm = 0;
	telemetry_listener.attached = 0;
}

odb_err odbtelem_poll(odbtelem_data *o_data) {
	if(!telemetry_listener.attached) {
		return ODB_EPIPE;
	}
	telemetry_shm *shm = telemtry_shared.shm;
	odbtelem_data *shm_datav = telemtry_shared.shm_datav;

	// if we're caught up we'll wait until the host broadcast an updated
	// raster position.
	futex_wait(&shm->futex_raster, telemetry_listener.raster);

	// did the host just shut down?
	if(!shm->hosted) {
		// yup.
		odbtelem_detach();
		return ODB_EPIPE;
	}
	// check for misses
	uint32_t shmraster = shm->futex_raster; // sense its voltile
	uint32_t diff = shmraster - telemetry_listener.raster;
	if(shmraster > telemetry_listener.raster) {
		// overflow
		log_debugf("telementry raster overflow, inverting raster missed check");
		diff = (UINT32_MAX - telemetry_listener.raster) + shmraster;
	}
	// Note we only known the approximate amount of misses because we don't
	// use mutexes to read from futex_raster. For all we know it could've
	// incremented 100 times sense the futex_wait was triggered.
	if(diff > shm->dataq) {
		log_alertf("telemetry raster missed (polling too slow): ~%d events "
		           "missed", diff);
		telemetry_listener.raster += diff;
		telemetry_listener.index = telemetry_listener.raster % shm->dataq;
		return ODB_EMISSED;
	}

	// read the data on this position.
	*o_data = shm_datav[telemetry_listener.index];

	// we've now polled the data from the raster.

	// increment our index/raster position.
	telemetry_listener.raster++;
	telemetry_listener.index = (telemetry_listener.index + 1) % shm->dataq;
	return 0;
}

odb_err odbtelem_image(odbtelem_image_t *o_image) {
	// todo: ODB_EVERSION
	if(!telemetry_listener.attached) {
		return ODB_EPIPE;
	}
	if(!o_image) {
		return ODB_EINVAL;
	}

	// get the image from the shm. To avoid mutli-thread tearing, we use a
	// technique I like to call "a brute's mutex".
	do {
		// download the image from the shm
		*o_image = telemtry_shared.shm->image;

		// sense as per image_raster's documentation. If the futex_raster is
		// ever not equal to image_raster, this means the host in the middle
		// of updating shm->image. Thus, we'll just keep trying until we know
		// no thread-tearing has happened.
	} while(telemtry_shared.shm->futex_raster
	        != telemtry_shared.shm->image_raster);


	// actualize the pointers
	o_image->job_desc = (void *)telemtry_shared.shm
	                    + (uint64_t)o_image->job_desc;
	o_image->job_workersv = (void *)telemtry_shared.shm
	                        + (uint64_t)o_image->job_workersv;

	// todo!: other pointers

	return 0;
}

// installs the data into the buffer. Will override old data.
static void odbtelem_install(odbtelem_data data) {
	telemetry_shm *shm = telemtry_shared.shm;
	odbtelem_data *shm_datav = telemtry_shared.shm_datav;
	pthread_mutex_t *mutex = &telemetry_host.mutex;
	pthread_mutex_lock(mutex);
	shm->index = (shm->index + 1) % shm->dataq;
	shm->futex_raster++;
	shm_datav[shm->index] = data;

	// todo image


	// update the image raster
	shm->image_raster++;

	pthread_mutex_unlock(mutex);
	futex_wake(&shm->futex_raster, INT32_MAX);
}

inline static void destroyshmbuffer() {
#ifdef EDBTELEM_INNERPROC
	int innerprocess = startedparams.innerprocess;
#else
	int innerprocess = 0;
#endif
	telemtry_shared.shm->hosted = 0;

	// increment to let listeners know we're shutting down.
	telemtry_shared.shm->futex_raster++;
	futex_wake(&telemtry_shared.shm->futex_raster, INT32_MAX);
	if(innerprocess) {
		munmap(telemtry_shared.shm, telemtry_shared.shm->shmc);
		shm_unlink(telemtry_shared.shm_name);
	} else {
		free(telemtry_shared.shm);
	}
	pthread_mutex_destroy(&telemetry_host.mutex);
	telemtry_shared.shm = 0;
}

// assumes buffer has not already been set.
// Assumes started params has been set.
inline static odb_err setshmbuffer() {

#ifdef EDBTELEM_INNERPROC
	int innerprocess = startedparams.innerprocess;
#else
	int innerprocess = 0;
#endif
	int buffersize   = (1 << startedparams.buffersize_exp);
	int size = (int)sizeof(telemetry_shm) +
	           buffersize * (int)sizeof(odbtelem_data);

	// allocate telemtry_shared.shm
	if(innerprocess) {
		shmname(getpid(), telemtry_shared.shm_name);
		int shmfd = shm_open(telemtry_shared.shm_name,
		                     O_RDWR | O_CREAT | O_EXCL,
		                     0666);
		if (shmfd == -1) {
			log_critf("shm_open");
			return ODB_ECRIT;
		}

		// mmap the shm
		if(ftruncate(shmfd, size) == -1) {
			log_critf("ftruncate");
			return ODB_ECRIT;
		}

		telemtry_shared.shm = mmap(0, size,
		                           PROT_READ,
		                           MAP_SHARED,
		                           shmfd, 0);
		close(shmfd);
		if (telemtry_shared.shm == MAP_FAILED) {
			log_critf("mmap");
			telemtry_shared.shm = 0;
			shm_unlink(telemtry_shared.shm_name);
			return ODB_ECRIT;
		}
	}else{
		telemtry_shared.shm = malloc(size);
	}
	telemtry_shared.shm_datav = (void *)telemtry_shared.shm
	                            + sizeof(telemetry_shm);


	// initialize mutex
	pthread_mutex_init(&telemetry_host.mutex, 0);

	// do this last to show we're only now ready
	telemetry_shm head = {
			.magicnum = telemetry_shm_magicnum,
			.shmc = size,
			.dataq = buffersize,
			.futex_raster = 0,
			.image = {0},
			.image_raster = 0,
			.hosted = 1,
			.index = 0,
	};
	memcpy(telemtry_shared.shm, &head,
	       sizeof(telemetry_shm)); // by-pass constants
	return 0;
}

odb_err odbtelem(int enabled, odbtelem_params_t params) {
#ifndef EDBTELEM
	return ODB_EVERSION;
#endif
	odb_err err;
	if(telemenabled && enabled) return 0;
	if(!telemenabled && !enabled) return 0;
	if(enabled) {
		// We are going from enabled to disabled.
		destroyshmbuffer();
	} else {
		// We are going from disabled to enabled.
		if(params.buffersize_exp < 0) return ODB_EINVAL;
		if(params.buffersize_exp > 15) return ODB_EINVAL;
#ifndef EDBTELEM_INNERPROC
		if(params.innerprocess) return ODB_EVERSION;
#endif
		startedparams = params;
		if((err = setshmbuffer())) {
			return err;
		}
	}
	telemenabled = enabled;
	return 0;
}

void telemetry_pages_newobj(unsigned int entryid,
                            odb_pid startpid, unsigned int straitc) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_PAGES_NEWOBJ,
			.entryid = entryid,
			.pageid = startpid,
			.pagec = straitc,
	};
	odb_data_process(d);
}
void telemetry_pages_newdel(odb_pid startpid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_PAGES_NEWDEL,
			.pageid = startpid,
	};
	odb_data_process(d);
}
void telemetry_pages_cached(odb_pid pid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_PAGES_CACHED,
			.pageid = pid,
	};
	odb_data_process(d);
}
void telemetry_pages_decached(odb_pid pid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_PAGES_DECACHED,
			.pageid = pid,
	};
	odb_data_process(d);
}
void telemetry_workr_accepted(unsigned int workerid, unsigned int jobslot) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_WORKR_ACCEPTED,
			.workerid = workerid,
			.jobslot = jobslot,
	};
	odb_data_process(d);
}
void telemetry_workr_pload(unsigned int workerid, odb_pid pageid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_WORKR_PLOAD,
			.workerid = workerid,
			.pageid = pageid
	};
	odb_data_process(d);
}
void telemetry_workr_punload(unsigned int workerid, odb_pid pageid) {
	if(!telemenabled) return;
	odbtelem_data d = {
			.class = ODBTELEM_WORKR_PUNLOAD,
			.workerid = workerid,
			.pageid = pageid
	};
	odb_data_process(d);
}