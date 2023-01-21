#ifndef EDB_DEBUG_H
#define EDB_DEBUG_H
#include "../glplotter/glplotter.h"
#include "../text.h"
typedef struct {
	int width,height;
	text_font font;
	unsigned int frameid;
	char buff[255];
	glp_eventtype_t lastevent;
} ent_debug_t;

void ent_debug_new(ent_debug_t *o_ent);

#endif //EDB_DEBUG_H
