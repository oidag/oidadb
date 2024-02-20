#ifndef ODB_COMMON_H_
#define ODB_COMMON_H_

#include <stdint.h>

#define export __attribute__((__visibility__("default")))

/** @name ID Types
 * Note that these typedefs are by-definition. These will be the same across
 * all builds of all architectures.
 *
 * \{
 */
/// dynamic data pointer
typedef uint64_t odb_dyptr;
/// (o)bject (id)
typedef uint64_t odb_oid;
/// (s)tructure (id)
typedef uint16_t odb_sid;
/// (e)ntity (id)
typedef uint16_t odb_eid;
/// (r)ow (id)
typedef uint64_t odb_rid;
/// (p)age (id)
typedef uint64_t odb_pid;
///\}

typedef uint64_t odb_revision;

// odbh is incomplete and a private structure.
typedef struct odbh odbh;


typedef uint8_t odb_type;
#define ODB_ELMINIT  0
#define ODB_ELMDEL   1
#define ODB_ELMSTRCT 2
#define ODB_ELMTRASH 3
#define ODB_ELMOBJ   4
#define ODB_ELMENTS  5
#define ODB_ELMPEND  6
#define ODB_ELMLOOKUP 7
#define ODB_ELMDYN 8
#define ODB_ELMOBJPAGE 9
#define ODB_ELMRSVD 10

#endif