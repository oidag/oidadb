#ifndef _edbd_u_h
#define _edbd_u_h

#include "edbd.h"
#include "odb-structures.h"

// note: at this time these functions only work on database creation.
// once we get to extending index/structure pages after the database has been
// created, then these prototypes need to be modified to include
// trashvors/plefts
void edbd_u_initindexpage(void *page, unsigned int psize);
void edbd_u_initstructpage(void *page, unsigned int pszie);
// same as edbd_u_initindexpage but also puts in the reserved enteries
void edbd_u_initindex_rsvdentries(void *page,
                                  unsigned int psize,
								  edb_pid indexstart,
								  edb_pid structstart,
                                  unsigned int indexpagec,
                                  unsigned int structurepagec);

#endif