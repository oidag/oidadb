#include <stdio.h>
#include "ents_u.h"
#include <GL/gl.h>
#include "ents.h"
#include "colors.h"
#include "../dbfile/dbfile.h"

typedef struct {
	recti_t viewport;
	int width;
	int height;
} pagecontents;

ent_pager _pager;
const ent_pager *pager = 0;

static void event(graphic_t *g, eventdata_t e) {
	switch (e.type) {
		case DAF_ONMOUSE_DOWN:
			if(pager->ishover) {
				// on hover.
			}
			break;
		default:
		case DAF_ONMOUSE_MOVE:
			_pager.ishover = rect_contains(glp_viewportget(g), e.pos);
			break;
	}

}

static void setviewport(graphic_t *g, eventdata_t _) {
	// move it to the middle of the screen
	vec2i size = glplotter_size();
	int width = (size.width/12)*4;
	int height = size.height;
	_pager.vp = (recti_t) {
			0,
			0,
			width,
			height
	};
	glp_viewport(g, _pager.vp);

}

static void draw(graphic_t *g){
	color_t lcolor = color_slate800;
	color_t rcolor = color_slate700;

	// draw overall panel
	glPushMatrix();

	glLoadIdentity();
	color_glset(lcolor);
	glOrtho(0,1,0,1,1,-1);
	glBegin(GL_QUADS);
	glVertex2f(0,0);
	glVertex2f(0,1);
	color_glset(rcolor);
	glVertex2f(1,1);
	glVertex2f(1,0);
	glEnd();

	glPopMatrix();
}

void ent_pager_new() {
	pager = &_pager;
	graphic_t *g = glp_new();
	// initialize structure

	// glp set up
	setviewport(g, (eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_name(g, "pager");
	glp_events(g, DAF_ONMOUSE_MOVE, event);
	glp_events(g, DAF_ONMOUSE_DOWN, event);
	glp_events(g, DAF_ONWINDOWSIZE, setviewport);



	// start the children
	ent_pagerfhead_start();

	ent_dialog_start();

	ent_page_new();
}