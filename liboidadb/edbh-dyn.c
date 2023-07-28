#include <oidadb/oidadb.h>
struct odbh_jobret odbh_jdyn_read(odbh *handle
		, odb_oid oid
		, int idx
		, void *datv
		, int datc) {

	struct odbh_jobret ret;
	ret.err = ODB_EVERSION;
	return ret;
}
struct odbh_jobret odbh_jdyn_write(odbh *handle
		, odb_oid oid
		, int idx
		, const void *datv
		, int datc) {
	struct odbh_jobret ret;
	ret.err = ODB_EVERSION;
	return ret;
}
struct odbh_jobret odbh_jdyn_free(odbh *handle
		, odb_oid oid
		, int idx) {
	struct odbh_jobret ret;
	ret.err = ODB_EVERSION;
	return ret;
}