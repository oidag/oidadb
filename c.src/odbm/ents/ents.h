#ifndef ENTS_H_
#define ENTS_H_

#include <bits/types/time_t.h>
#include "colors.h"

#include "../glplotter/glplotter.h"
#include "../text.h"
typedef struct {
	int width,height;
	text_font font;
	unsigned int frameid;
	char buff[255];
	glp_eventtype_t lastevent;

	time_t       frame_last_rec;
	unsigned int frame_last_recid;
	unsigned int fps;
} ent_debug_t;

void ent_debug_new(ent_debug_t *o_ent);

typedef struct ent_background_t {
	int _;
} ent_background_t;

int ent_background_new(ent_background_t *o_bg);

void ent_opener_new(void (*onload)());

// abducts stderr and stdout
void ent_terminal_start();


#endif