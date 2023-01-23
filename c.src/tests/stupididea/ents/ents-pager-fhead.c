#include "ents_u.h"

static void viewport(graphic_t *g, eventdata_t e) {
	int h = pager->vp.heigth/8*1;
	recti_t v = {
			pager->vp.x,
			pager->vp.y + pager->vp.heigth - h,
			pager->vp.width/8*3,
			h,
	};
	glp_viewport(g, v);
}
static void draw(graphic_t *g) {

	// fhead
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,1,0,1,1,-1);
	glBegin(GL_QUADS);
	color_glset(color_violet900);
	glVertex2f(0,0);
	glVertex2f(0,1);
	glVertex2f(1,1);
	glVertex2f(1,0);
	glEnd();
	glPopMatrix();
}

static void events(graphic_t *g, eventdata_t e);

static struct {

} fhead;

void ent_pagerfhead_start() {
	graphic_t *g = glp_new();
	glp_name(g, "pagehead");
	viewport(g,(eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONWINDOWSIZE, viewport);
}