#include <oidadb-internal/options.h>
#include "blocks.h"


odb_err odbh_buffer_map(odb_buf *buffer
                        , void **mdata
                        , unsigned int boff
                        , unsigned int blockc) {

	struct odb_buffer_info info = buffer->info;

	if (blockc > info.bcount || boff + blockc > info.bcount) {
		return ODB_EOUTBOUNDS;
	}

	// check for ODB_EMAPPED
	for (unsigned int i = boff; i < boff + blockc; i++) {
		unsigned int bitoff    = i % 32;
		uint32_t     statebits = buffer->map_statev[i / 32];
		if ((statebits >> bitoff) & 1) {
			return ODB_EMAPPED;
		}
	}

	*mdata = (void *) buffer->user_datam + boff * ODB_BLOCKSIZE;

	// update the maping states
	for (unsigned int i = boff; i < boff + blockc; i++) {
		unsigned int bitoff     = i % 32;
		uint32_t     *statebits = &buffer->map_statev[i / 32];
		*statebits = *statebits | (1 << bitoff);
	}

	return 0;
}

odb_err odbh_buffer_unmap(odb_buf *buffer
                          , unsigned int boff
                          , unsigned int blockc) {

	struct odb_buffer_info info = buffer->info;

	if (blockc > info.bcount || boff + blockc > info.bcount) {
		return ODB_EOUTBOUNDS;
	}

	// check for ODB_ENMAP
	for (unsigned int i = boff; i < boff + blockc; i++) {
		unsigned int bitoff    = i % 32;
		uint32_t     statebits = buffer->map_statev[i / 32];
		if (!((statebits >> bitoff) & 1)) {
			return ODB_ENMAP;
		}
	}

	// right now, there's no special task that we need to do to unmap... but:
	// later: such as doing things over the network, we'll probably need to
	//  do some memory stuff right here.


	// update the maping states
	for (unsigned int i = boff; i < boff + blockc; i++) {
		unsigned int bitoff     = i % 32;
		uint32_t     *statebits = &buffer->map_statev[i / 32];
		*statebits = *statebits & ~(1 << bitoff);
	}

	return 0;
}


