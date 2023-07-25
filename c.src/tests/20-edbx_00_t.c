
#include <stdio.h>
#include <pthread.h>
#include <wait.h>
#include <stdlib.h>
#include "teststuff.h"
#include "../include/oidadb.h"
#include "../include/telemetry.h"
#include "../wrappers.h"

struct userd {
	char username[50];
	char password[32];
	char email[25];
	int gender;
};
struct orders {
	odb_oid user;
	int productid;
	int quantity;
	double price;
};

struct threadpayload {
	pthread_t thread;
	int randr;
};

static struct shmobj {
} shmobj;
_Atomic unsigned int jobscomplete = 0;

// forward declarations
pthread_t hostthread;
uint32_t host_futex = 0;
void *func_hostthread(void *v);
void *handlethread(void *payload);

////////////////////////////////////////////////////////////////////////////////
static const int handle_procs = 0; // how many extra processes to start (0
// means
// none)
static const int handle_threads = 1; // how many threads to start PER THREAD (0
// means none, meaning nothing will be done)

const int host_workers = 1;

const int stkjobs = 3; // not actually used
const int entjobs = 3; // not actually used. just needs to be 2
int userlen = 100;
int readwritejobs = 10 * 4; // must be amultiple of 4.

int orderslen = 100;

void telem_jcomplete(struct odbtelem_data data) {
	jobscomplete++;
}

void test_main() {
	srand(3713713);
	test_log("creating oidadb file...");
	struct odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return;
	}

	odbtelem(1, (struct odbtelem_params){0});
	odbtelem_bind(ODBTELEM_JOBS_COMPLETED, telem_jcomplete);

	test_log("starting host thread...");
	pthread_create(&hostthread, 0, func_hostthread, 0);

	test_log("waiting for host bootup...");
	futex_wait(&host_futex, 0);
	switch (host_futex) {
		default:
		case ODB_VERROR:
			test_error("host futext fed error");
			return;
		case ODB_VACTIVE:
			test_log("...host booted up.");
			break;
	}

	// atp: Our host thread is going strong. now time to create handles.

	// start the processes
	test_log("launching %d handle processes...", handle_procs);
	pid_t mypid = getpid();
	for(int i = 0; i < handle_procs; i++) {
		mypid = fork();
		if(mypid == 0) {
			break; // child
		}
	}

	// start the threads
	struct threadpayload pthreads[handle_threads];
	test_log("launching %d handle threads...", handle_threads);
	for(int i = 0; i < handle_threads; i++) {
		pthreads[i].randr = rand();
		pthread_create(&pthreads[i].thread, 0, handlethread, &pthreads[i]);
		test_log("... created t%lx", pthreads[i].thread);
	}

	test_log("waiting for threads to join...");
	for(int i = 0; i < handle_threads; i++) {
		pthread_join(pthreads[i].thread, 0);
		test_log("... joined t%lx", pthreads[i].thread);
	}

	// join procsses
	if(mypid == 0) {
		// child. so we return.
		return;
	}
	test_log("waiting for processes to join...");
	for(int i = 0; i < handle_procs; i++) {
		int ret;
		wait(&ret);
		if(ret) {
			test_error("process returned error");
		}
	}

	// make sure everything was created
	// note, this only accounts for the jobs executed on the main process, sense telemtry only
	// works on the process the host is on.
	int totaljobs = (stkjobs + entjobs + userlen + readwritejobs + orderslen) * handle_threads * (handle_procs+1);
	if(jobscomplete != totaljobs) {
		test_error("total jobs not expected: expected %d, got %d", totaljobs, jobscomplete);
		return;
	}

	test_log("closing host...");
	err = odb_hoststop();
	if(err) {
		test_error("failed to close host");
		return;
	}
	pthread_join(hostthread, 0);
	test_log("...closed.");
	return;
}

void *func_hostthread(void *v) {
	struct odb_hostconfig config = odb_hostconfig_default;
	//config.slot_count=1;
	config.stat_futex = &host_futex;
	config.worker_poolsize=host_workers;
	odb_err err1 = odb_host(test_filenmae, config);
	if(err1) {
		test_error("odb_host returned error %d", err1);
	}
	return 0;
}

void *handlethread(void *payload) {
	struct threadpayload* pl = payload;
	unsigned int randr = pl->randr;
	struct odbh *handle;
	odb_err err1;
	err1 = odb_handle(test_filenmae, &handle);
	if(err1) {
		test_error("error: %s", odb_errstr(err1));
	}

	// datashit.
	odb_oid uids[userlen];
	odb_oid orderids[orderslen];

	// create a "users" object.
	odb_sid usersid;
	odb_sid ordersid;

	// user structure
	struct odb_structstat stat;
	stat.objc = sizeof(struct userd);
	stat.dynmc  = 0;
	stat.confc  = 0;
	struct odbh_jobret jret = odbh_jstk_create(handle, stat);
	if(jret.err) {
		test_error("stk_create error: %d", jret.err);
		goto close;
	}
	usersid = jret.sid;

	// orders structure
	struct odb_structstat ordersstat;
	ordersstat.objc = sizeof(struct orders);
	ordersstat.dynmc  = 0;
	ordersstat.confc  = 0;
	jret = odbh_jstk_create(handle, ordersstat);
	if(jret.err) {
		test_error("stk_create error: %d", jret.err);
		goto close;
	}
	ordersid = jret.sid;
	test_log("created users structure (%d) and orders structure (%d)", usersid,
			 ordersid);

	// create entities
	struct odb_entstat estat;
	estat.structureid = usersid;
	estat.type = ODB_ELMOBJ;
	jret = odbh_jent_create(handle, estat);
	if(jret.err) {
		test_error("odbh_jent_create: %d", jret.err);
		goto close;
	}
	odb_eid usereid = jret.eid;
	estat.structureid = ordersid;
	jret = odbh_jent_create(handle, estat);
	if(jret.err) {
		test_error("odbh_jent_create 2: %d", jret.err);
		goto close;
	}
	odb_eid ordereid = jret.eid;
	test_log("created users entitty (%d) and orders entity (%d)", usereid,
	         ordereid);

	// create some users
	for(int i = 0; i < userlen; i++) {
		struct userd u = {0};
		strcpy(u.email, "email@email.com");
		u.gender = rand_r(&randr);
		strcpy(u.password, "supersecret");
		sprintf(u.username, "user %d", i);
		jret = odbh_jobj_alloc(handle, usereid, &u);
		if(jret.err) {
			test_error("jobj %d", jret.err);
			goto close;
		}
		uids[i] = jret.oid;
	}

	// create some orders
	for(int i = 0; i < orderslen; i++) {
		struct orders o = {0};
		o.user = uids[rand_r(&randr) % userlen];
		o.price = (double)rand_r(&randr);
		o.productid = i % 40;
		o.quantity = rand_r(&randr) % 243;
		jret = odbh_jobj_alloc(handle, ordereid, &o);
		if(jret.err) {
			test_error("jobj 2 %d", jret.err);
			goto close;
		}
		orderids[i] = jret.oid;
	}

	// randomly read order data
	// /4 because each itoration has 4 jobs
	for(int i = 0; i < readwritejobs/4; i++) {
		odb_oid orderid = orderids[rand_r(&randr) % orderslen];
		struct orders o;
		jret = odbh_jobj_read(handle, orderid, &o);
		if(jret.err) {
			test_error("jobj 3 %d", jret.err);
			goto close;
		}
		struct userd u;
		jret = odbh_jobj_read(handle, o.user, &u);
		if(jret.err) {
			test_error("jobj 4 %d", jret.err);
			goto close;
		}
		// write an update to this user.
		u.gender++;
		jret = odbh_jobj_write(handle, o.user, &u);
		if(jret.err) {
			test_error("jobj 5 %d", jret.err);
			goto close;
		}
		int gendertest = u.gender;
		u.gender = 0;
		// make sure the write went through
		jret = odbh_jobj_read(handle, o.user, &u);
		if(jret.err) {
			test_error("jobj 6 %d", jret.err);
			goto close;
		}
		if(u.gender != gendertest) {
			test_error("jobj 7 %d", jret.err);
			goto close;
		}
	}

	close:
	odb_handleclose(handle);
	return 0;
}