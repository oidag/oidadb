#ifndef _edbd_u_h
#define _edbd_u_h

#include "edbd.h"
#include <oidadb-internal/odbfile.h>

// note: at this time these functions only work on database creation.
// once we get to extending index/structure pages after the database has been
// created, then these prototypes need to be modified to include
// trashvors/plefts
//
// edbd_u_initstructpage requires the pageid of page because it needs to build
// the linked list for trash.
void edbd_u_initindexpage(void *page, unsigned int psize);
void edbd_u_initstructpage(void *page, unsigned int pszie, odb_pid trashvor);
// same as edbd_u_initindexpage but also puts in the reserved enteries
void edbd_u_initindex_rsvdentries(void *page,
                                  unsigned int psize,
                                  odb_pid indexstart,
                                  odb_pid structstart,
                                  unsigned int indexpagec,
                                  unsigned int structurepagec);

#endif