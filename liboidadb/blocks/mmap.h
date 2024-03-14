#ifndef OIDADB_MMAP_H
#define OIDADB_MMAP_H

#include <oidadb/oidadb.h>
#include <sys/mman.h>

/**
 * The purpose of these wrappers is so we can implement some more verbose memory
 * debugging policies. We can also implement our own caching and stuff. But one
 * thing must a constant and that is the returned addresses must be completely
 * interchangeable with what they wrap.
 *
 * But otherwise, avoid using mmap(2) and malloc(3) in leu of these.
 */


/**
 * odb_mmap64 is wrapper of mmap(2). Right now it's only usefulness of being
 * an abstraction is that it performs all the page-to-offset calculations for you.
 *
 * But I plan on having some more features to it such as fixing this annoying
 * rule about mmap(2):
 *
 *  "It is unspecified whether changes made to the file after the mmap() call
 *   are visible in the mapped region."
 *
 * If MAP_FAILED is returned, see odb_mmap_errno
 */
void *odb_mmap (void *addr
				, unsigned int page_count
				, int prot
				, int flags
				, int fd
				, odb_pid page_offset);
void odb_munmap(void *addr, unsigned int page_count);

/**
 * alias for
 * odb_mmap(0
 *        , page_count
 *        , PROT_NONE
 *        , MAP_ANON | MAP_PRIVATE
 *        , -1
 *        , 0);
 *
 */
void *odb_mmap_alloc(unsigned int page_count);

/**
 * wrapper for malloc(3) and free(3) but instead reports errors to
 * odb_mmap_errno.
 */
void *odb_malloc(size_t size);
void odb_free(void *ptr);


/**
 * When using odb_mmap, do not use errno, use odb_mmap_errno
 * instead. This will return an odb_err instead of a normal system
 * error.
 */
odb_err *_odb_mmap_err_location();
#define odb_mmap_errno (*_odb_mmap_err_location ())

#endif //OIDADB_MMAP_H
