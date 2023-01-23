#include <stdio.h>
#include "ents_u.h"
#include <GL/gl.h>
#include "ents.h"
#include "colors.h"
#include "../dbfile/dbfile.h"

static struct {
	recti_t viewport;
	int width;
	int height;

	column_t *descriptor;
	column_t *pages;
	column_t *pagebuff;
	column_t *workers;
	column_t *jobs;
	column_t *events;

} host = {0};

static void setviewport(graphic_t *g, eventdata_t _) {
	// move it to the middle of the screen
	vec2i size = glplotter_size();
	int width = (size.width/12)*4;
	int height = size.height;
	glp_viewport(g, (recti_t) {
			0,
			0,
			width,
			height
	});

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

/*void ent_pagerfhead_start() {
	graphic_t *g = glp_new();
	glp_name(g, "pagehead");
	viewport(g,(eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONWINDOWSIZE, viewport);
}*/

void element_host_start() {
	graphic_t *g = glp_new();
	// initialize structure

	// graphic for the background.
	setviewport(g, (eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_name(g, "column-host");
	glp_events(g, DAF_ONWINDOWSIZE, setviewport);

	column_t *selected;

	// header column
	selected = host.descriptor = column_new();
	column_color(selected, color_pink400);
	column_shard_color(selected, color_pink300);
	column_width(selected, 2);
	column_type(selected, ELM_DESCRIPTOR);
	column_height(selected,2);
	column_pos(selected, 0, 14);

	// pages
	selected = host.pages = column_new();
	column_color(selected, color_violet900);
	column_shard_color(selected, color_violet300);
	column_width(selected, 1);
	column_type(selected, ELM_PAGE);
	column_height(selected,14);
	column_pos(selected, 0, 0);
	// pages (buff)
	selected = host.pagebuff = column_new();
	column_color(selected, color_violet700);
	column_shard_color(selected, color_violet300);
	column_width(selected, 1);
	column_type(selected, ELM_PAGE);
	column_height(selected,14);
	column_pos(selected, 1, 0);

	// todo: edbw

	// todo: edbs
}