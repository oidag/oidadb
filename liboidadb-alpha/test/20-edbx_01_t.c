
#include <stdio.h>
#include <pthread.h>
#include <wait.h>
#include <stdlib.h>
#include "teststuff.h"
#include <oidadb/oidadb.h>
#include "../wrappers.h"


struct threadpayload {
	pthread_t thread;
};

static struct shmobj {
} shmobj;

// forward declarations
pthread_t hostthread;
uint32_t host_futex = 0;
void *func_hostthread(void *v);
void *handlethread(void *payload);

////////////////////////////////////////////////////////////////////////////////
static const int handle_procs = 0; // how many extra processes to start (0
// means
// none)
static const int handle_threads = 5; // how many threads to start PER THREAD (0
// means none, meaning nothing will be done)

void test_main() {
	test_log("creating oidadb file...");
	struct odb_createparams createparams = odb_createparams_defaults;
	err = odb_create(test_filenmae, createparams);
	if (err) {
		test_error("failed to create file");
		return;
	}

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
			test_log("...done.");
			break;
	}

	// atp: Our host thread is going strong. now time to create handles.

	// start the processes
	test_log("launching %d handle processes...", handle_procs);
	pid_t mypid;
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
	config.stat_futex = &host_futex;
	odb_err err1 = odb_host(test_filenmae, config);
	if(err1) {
		test_error("odb_host returned error %d", err1);
	}
	return 0;
}

void *handlethread(void *payload) {
	struct odbh *handle;
	odb_err err1;
	err1 = odb_handle(test_filenmae, &handle);
	if(err1) {
		test_error("error: %d", err1);
	}

	// datashit.
	int userlen = 100;
	odb_oid uids[userlen];
	int orderslen = 1000;
	odb_oid orderids[orderslen];

	// create a "users" object.
	struct user {
		char username[50];
		char password[32];
		char email[25];
		int gender;
	};
	odb_sid usersid;
	struct orders {
		odb_oid user;
		int productid;
		int quantity;
		double price;
	};
	odb_sid ordersid;

	// user structure
	struct odb_structstat stat;
	stat.fixedc = sizeof(struct user);
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
	ordersstat.fixedc = sizeof(struct orders);
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
		struct user u = {0};
		strcpy(u.email, "email@email.com");
		u.gender = 69;
		strcpy(u.password, "supersecret");
		sprintf(u.username, "user %d", i);
		jret = odbh_jobj_alloc(handle, ordereid, &u);
		if(jret.err) {
			test_error("jobj %d", jret.err);
			goto close;
		}
		uids[i] = jret.oid;
	}

	// create some orders
	for(int i = 0; i < orderslen; i++) {
		struct orders o = {0};
		o.user = uids[rand() % userlen];
		o.price = (double)rand();
		o.productid = i % 40;
		o.quantity = rand() % 243;
		jret = odbh_jobj_alloc(handle, ordereid, &o);
		if(jret.err) {
			test_error("jobj 2 %d", jret.err);
			goto close;
		}
		orderids[i] = jret.oid;
	}

	// randomly read order data
	for(int i = 0; i < 100; i++) {
		odb_oid orderid = orderids[rand() % orderslen];
		struct orders o;
		jret = odbh_jobj_read(handle, orderid, &o);
		if(jret.err) {
			test_error("jobj 3 %d", jret.err);
			goto close;
		}
		struct user u;
		jret = odbh_jobj_read(handle, o.user, &u);
		if(jret.err) {
			test_error("jobj 4 %d", jret.err);
			goto close;
		}
		// make sure gender is what it should be.
		if(u.gender != 69) {
			test_error("gender is not 69");
			goto close;
		}
		// write an update to this user.
		u.gender++;
		jret = odbh_jobj_write(handle, o.user, &u);
		if(jret.err) {
			test_error("jobj 5 %d", jret.err);
			goto close;
		}
	}

	close:
	odb_handleclose(handle);
	return 0;
}