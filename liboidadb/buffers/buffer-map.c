#include <oidadb-internal/options.h>
#include <oidadb-internal/buffers.h>
#include <oidadb-internal/memory.h>
#include "oidadb-internal/errors.h"


// How much odb_buf.mapped_regionsq will grow when full
#define ODB_BUFFER_MAP_LIST_GROWTH 1

// Returns 1 if the region is 100% mapped, 0 if any less than that
static int is_region_mapped(const odb_buf *buffer
                     , uint64_t byte_start
                     , uint64_t byte_count) {


	int regionsc                = buffer->mapped_regionsc;
	odb_buf_map_region *regionv = buffer->mapped_regionsv;
	uint64_t byte_end           = byte_start + byte_count;

	for(int i = 0; i < regionsc; i++) {
		uint64_t region_start = regionv[i].start_offset;
		uint64_t region_end = regionv[i].end_offset;

		if(byte_start >= region_start && byte_end <= region_end) {
			return 1;
		}
	}

	return 0;
}


// Returns 1 if the region is 100% unmapped, 0 if any less than that
static int is_region_unmapped(const odb_buf *buffer
                              , uint64_t byte_start
                              , uint64_t byte_count) {

	int regionsc                = buffer->mapped_regionsc;
	odb_buf_map_region *regionv = buffer->mapped_regionsv;
	uint64_t byte_end           = byte_start + byte_count;

	for(int i = 0; i < regionsc; i++) {
		uint64_t region_start = regionv[i].start_offset;
		uint64_t region_end = regionv[i].end_offset;

		if(byte_start >= region_start && byte_start < region_end) {
			return 0;
		}

		// doesn't start within a region... but does it end within a region?

		if(byte_end > region_start && byte_end <= region_end) {
			return 0;
		}
	}

	return 1;
}

// appeneds a buffer->mapped_region array. Helper method for mark_as_mapped and
// mark_as_unmapped.
// returns odb_mmap_errno if there was a problem with memory (if that's the case,
// buffer is left unaffected)
//
// later: idk if I need to worry about the speed of looking up these maps in the
//  future, but I'm sure there's some tricky b-tree operations I could implement
//  along with a forced order-by with _appendmap.
static odb_err _appendmap(odb_buf *buffer, uint64_t byte_start, uint64_t byte_end) {

	if(buffer->mapped_regionsq == buffer->mapped_regionsc) {

		// mapped_regions buffer is full, realloc some padding

		int newsize   = buffer->mapped_regionsq + ODB_BUFFER_MAP_LIST_GROWTH;
		void *newbuff = odb_realloc(buffer->mapped_regionsv, newsize * sizeof(odb_buf_map_region));
		if(!newbuff) {
			return odb_mmap_errno;
		}
		buffer->mapped_regionsv = newbuff;
		buffer->mapped_regionsq = newsize;
	}

	buffer->mapped_regionsv[buffer->mapped_regionsc] = (odb_buf_map_region){
			.start_offset = byte_start,
			.end_offset = byte_end,
	};
	buffer->mapped_regionsc++;
	return 0;
}

// marks the region as mapped in the buffer, and it will do any consolidation
// that's needed.
// assumes the region is not mapped at all
// will return an error if out of memory.
static odb_err mark_as_mapped(odb_buf *buffer
                          , uint64_t byte_start
                          , uint64_t byte_count) {

	uint64_t byte_end = byte_start + byte_count;

	assert(byte_start < byte_end);

	// first, see if we can extend any existing map
	for(int i = 0; i < buffer->mapped_regionsc; i++) {
		uint64_t region_start = buffer->mapped_regionsv[i].start_offset;
		uint64_t region_end = buffer->mapped_regionsv[i].end_offset;

		assert(region_start < region_end);

		if(byte_start == region_end) {
			// we can extend this maping
			buffer->mapped_regionsv[i].end_offset = byte_end;
			return 0;
		}

		if (byte_end == region_start) {
			buffer->mapped_regionsv[i].start_offset = byte_start;
			return 0;
		}
	}

	// need to create a new mapping.
	return _appendmap(buffer, byte_start, byte_end);
}

// assumes the region is completely mapped already
// will handle any shrinking of existing regions, or otherwise splitting regions.
static odb_err mark_as_unmapped(odb_buf *buffer
                          , uint64_t byte_start
                          , uint64_t byte_count) {

	uint64_t byte_end = byte_start + byte_count;

	assert(byte_start < byte_end);

	// first, see if we can shrink any existing map
	int i;
	for(i = 0; i < buffer->mapped_regionsc; i++) {
		int shrankregion = 0;
		uint64_t region_start = buffer->mapped_regionsv[i].start_offset;
		uint64_t region_end = buffer->mapped_regionsv[i].end_offset;

		assert(region_start < region_end);

		// does the unmap lay in this region? If so, we need to split it.
		if(byte_start > region_start && byte_end < region_end) {
			break;
		}

		if(byte_start == region_start) {
			buffer->mapped_regionsv[i].start_offset = byte_end;
			shrankregion = 1;
		} else if (byte_end == region_end) {
			buffer->mapped_regionsv[i].end_offset = byte_start;
			shrankregion = 1;
		}

		// if we shrank an existing region, there's a chance we shrank the whole
		// region to the point where there's nothing left in the region, in such
		// case, delete the region.
		if(shrankregion) {
			if(buffer->mapped_regionsv[i].end_offset - buffer->mapped_regionsv[i].start_offset != 0) {
				// there's still region left in here. So nothing else we have
				// to do.
				return 0;
			}

			// we've shrank the region down to 0 now we need to delete it from
			// the regionsv array.

			for(int j = i; j+1 < buffer->mapped_regionsc; j++) {
				buffer->mapped_regionsv[j] = buffer->mapped_regionsv[j+1];
			}
			buffer->mapped_regionsc--;
			// later: maybe at somepoint we decrement regionsq here too?
			return 0;
		}
	}

	// at no point should the above for loop reach the end of the loop provided
	// the assumptions as documented on this function.
	assert(i != buffer->mapped_regionsc);

	// cannot shrink the map. So we must split it. create a new map to the right
	// of new hole and shink the existing map to the left of it.
	odb_err err = _appendmap(buffer, byte_end, buffer->mapped_regionsv[i].end_offset);
	if (err) {
		return err;
	}
	buffer->mapped_regionsv[i].end_offset = byte_start;
	return 0;
}

odb_err odbv_buffer_map(odb_buf *buffer
                        , void **mdata
                        , uint64_t byte_offset
                        , uint64_t byte_count) {

	const struct odb_buffer_info info = buffer->info;

	if(byte_count == 0) {
		return ODB_EINVAL;
	}

	// We put the || statement here in the case that byte_offset + byte_count
	// overflows.
	if (byte_count > info.buffer_data_size || byte_offset + byte_count > info.buffer_data_size) {
		return ODB_EOUTBOUNDS;
	}

	// check for ODB_EMAPPED
	if(!is_region_unmapped(buffer, byte_offset, byte_count)) {
		return ODB_EMAPPED;
	}

	*mdata = (void *) buffer->user_datam + byte_offset;

	// update the maping states
	return mark_as_mapped(buffer, byte_offset, byte_count);
}

odb_err odbv_buffer_unmap(odb_buf *buffer
                          , uint64_t byte_offset
                          , uint64_t byte_count) {

	if(byte_count == 0) {
		return ODB_EINVAL;
	}

	struct odb_buffer_info info = buffer->info;

	if (byte_count > info.buffer_data_size || byte_offset + byte_count > info.buffer_data_size) {
		return ODB_EOUTBOUNDS;
	}

	// check for ODB_ENMAP
	if (!is_region_mapped(buffer, byte_offset, byte_count, 1)) {
		return ODB_ENMAP;
	}

	// right now, there's no special task that we need to do to unmap... but:
	// later: such as doing things over the network, we'll probably need to
	//  do some memory stuff right here.


	// update the maping states
	return mark_as_unmapped(buffer, byte_offset, byte_count);
}


