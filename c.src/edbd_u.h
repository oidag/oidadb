#ifndef _edbd_u_h
#define _edbd_u_h

void edbd_u_initindexpage(void *page, unsigned int psize);
void edbd_u_initstructpage(void *page, unsigned int pszie);
// same as edbd_u_initindexpage but also puts in the reserved enteries
void edbd_u_initindex_rsvdentries(void *page,
                                  unsigned int psize,
                                  unsigned int structurepagec,
                                  unsigned int indexpagec);

#endif