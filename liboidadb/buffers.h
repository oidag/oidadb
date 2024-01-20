#ifndef OIDADB_BUFFERSP_H
#define OIDADB_BUFFERSP_H

typedef struct odb_buf {
	struct odb_buffer_info info;

	// bidv will never be nil and will always be filled with the block ids
	// associative to revsionv and blockv.
	//
	// if info->usage is ODB_UBLOCKS then blockv will be mapped. Otherwise, with
	// ODB_UVERIONS, revisionsv will be mapped.
	//
	// all arrays will have info.bcount elements.
	//odb_bid      *bidv;
	odb_revision *revisionv;
	void *blockv;

} odb_buf;

#endif //OIDADB_BUFFERSP_H
