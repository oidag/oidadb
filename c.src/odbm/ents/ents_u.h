#ifndef ENTS_H_U_
#define ENTS_H_U_
#include "elements.h"
#include "ents.h"

// include the standard utilities we always need.
#include <GL/gl.h>

#include "colors.h"
#include "../glplotter/glplotter.h"
#include "elements_u.h"


/*
 // ent_*_new: means you can call it multiple times and create new things each time
 // ent_*_start: singleton. only one can exist at a time.
 // template:

static void viewport(graphic_t *g, eventdata_t e);
static void draw(graphic_t *g);
static void events(graphic_t *g, eventdata_t e);

// start
void ent_***_new/start() {
 graphic_t *g = glp_new();
	glp_name(g, "NAME");
	viewport(g,(eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONWINDOWSIZE, viewport);
}

 */
typedef struct ent_pager {
	glp_viewport_t vp;
	int ishover;
} ent_pager;
const extern ent_pager *pager;
void ent_pagerfhead_start();

void ent_dialog_start();



#endif