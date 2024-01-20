#ifndef OIDADB_BLOCKS_H
#define OIDADB_BLOCKS_H

#include <oidadb/oidadb.h>

/**
 * Will lock the given blocks so that no other process can modify them.
 */
odb_err blocks_lock(odb_desc *desc, odb_bid bid, int blockc);

void blocks_unlock(odb_desc *desc, odb_bid bid, int blockc);

/**
 * will return ODB_EVERSION if there is a version mis-match between the proposed
 * vers and the current version of the given blocks.
 */
odb_err blocks_match_versions(odb_desc *desc
                              , odb_bid bid
                              , int blockc
                              , const odb_revision *vers);

/**
 * Takes the given blocks and backs them up, so the next call to blocks_rollback
 * will revert any changes made by blocks_commit_attempt.
 */
odb_err blocks_backup(odb_desc *desc, odb_bid bid, int blockc);

odb_err blocks_rollback(odb_desc *desc, odb_bid bid, int blockc);

/**
 * attempts to update all blocks provided in the bidv array with the associative
 * blockv array. If this function returns an error, you should definitely call
 * blocks_rollback. If successful, then all the blocks were successfully updated
 */
odb_err blocks_commit_attempt(odb_desc *desc
                              , odb_bid bid
                              , int blockc
                              , const void *blockv);

odb_err
blocks_versions(odb_desc *desc, odb_bid bid, int blockc, odb_revision *o_verv);

odb_err blocks_copy(odb_desc *desc, odb_bid bid, int blockc, void *o_blockv);


#endif //OIDADB_BLOCKS_H
