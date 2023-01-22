#ifndef ENTS_H_
#define ENTS_H_

#include "colors.h"

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

typedef struct ent_background_t {
	int _;
} ent_background_t;

int ent_background_new(ent_background_t *o_bg);

void ent_opener_new(void (*onload)());
void ent_pager_new();


#endif