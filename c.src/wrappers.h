#ifndef ODB_WRAPPERS_H_
#define ODB_WRAPPERS_H_
#include "errors.h"

#include <unistd.h>
#include <syscall.h>
#include <linux/futex.h>
#include <errno.h>

// wrappers. See futex(2)
//
// futex_wait returns -1 if EAGAIN was returned (which is not really an error)
// all other errors are set by errno and returns -1.
static int futex_wait(uint32_t *uaddr, uint32_t val) {
	int err = (int)syscall(SYS_futex, uaddr, FUTEX_WAIT, val, 0, 0, 0);
	if (err == -1 && errno != EAGAIN) {
		log_critf("futex returned errno: %d", errno);
		return -1;
	}
	// reset errno to keep it clean.
	if(err == -1) {
		errno = 0;
	}
	return err;
}

// same as futex_wait, except if it ends up waiting will be equiped with a
// bitset, see futex_wake_bitset to learn about that.
//
// wait_bitset must not be 0.
static int futex_wait_bitset(uint32_t *uaddr, uint32_t val,
                             uint32_t wait_bitset) {
	int err = (int)syscall(SYS_futex, uaddr, FUTEX_WAIT_BITSET, val, 0, 0,
	                       wait_bitset);
	if (err == -1 && errno != EAGAIN) {
		log_critf("critical futex_wait: %d", errno);
		return 0;
	}
	// reset errno to keep it clean.
	if(err == -1) {
		errno = 0;
	}
	return err;
}
// returns the count of waiters that were woken
static int futex_wake(uint32_t *uaddr, uint32_t count) {
	int ret = (int)syscall(SYS_futex, uaddr, FUTEX_WAKE, count, 0, 0, 0);
	return ret;
}
// same as futex_wake, but also:
//
// will only wake up waiters on futex_wait_bitset in which ~wait_bitset &
// wake_bitset~ returns true. Thus supplying (uint32_t)-1 will wake all
// bitset waiters.
//
// This will also wake any and all normal futex_wait-ers of uaddr.
// Conversely, a normal futex_wake will wake any and all futex_wait_bitset.
//
// wake_bitset must not be 0.
static int futex_wake_bitset(uint32_t *uaddr, uint32_t count,
                             uint32_t wake_bitset) {
	int ret = (int)syscall(SYS_futex, uaddr, FUTEX_WAKE_BITSET, count, 0, 0,
	                       wake_bitset);
	return ret;
}
#endif