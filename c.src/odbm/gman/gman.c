#include <stdio.h>
#include <GL/gl.h>
#include "gman_u.h"

#include "dialog.h"


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

int gman_init() {

	int err = 0;
	if((err = glplotter_init())) {
		return err;
	}


	// statics
	background_start();
	ent_terminal_start();

	// columns

	//graphic_t *g = glp_new();
	// initialize structure

	// graphic for the background.
	/*setviewport(g, (eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_name(g, "column-host");
	glp_events(g, DAF_ONWINDOWSIZE, setviewport);*/

	column_t *selected;
	// header column
	selected = host.descriptor = column_new();
	column_color(selected, color_pink400);
	column_type(selected, ELM_DESCRIPTOR);
	column_viewboxr(selected, (recti_12x16) {0, 14, 2, 2});
	// pages
	selected = host.pages = column_new();
	column_color(selected, color_violet900);
	column_type(selected, ELM_PAGE);
	column_viewboxr(selected, (recti_12x16) {0, 0, 1, 14});
	// pages (cached)
	selected = host.pagebuff = column_new();
	column_color(selected, color_violet700);
	column_type(selected, ELM_PAGE);
	column_viewboxr(selected, (recti_12x16) {1, 0, 1, 14});

	// todo: remove this... just for testing...
	shard_t *s = shard_new(host.pages);
	shard_new(host.pages);
	shard_new(host.pages);
	shard_new(host.pages);
	shard_new(host.pages);
	shard_t *s1 = shard_new(host.pages);
	shard_t *s2 = shard_new(host.pagebuff);
	ent_arrow_new(s1, s2);


	dialog_index_start();



	// edbw
	selected = host.workers = column_new();
	column_color(selected, color_emerald900);
	column_type(selected, ELM_WORKER);
	column_viewboxr(selected, (recti_12x16) {2, 0, 1, 16});

	// edbs-jobs
	selected = host.jobs = column_new();
	column_color(selected, color_cyan900);
	column_type(selected, ELM_WORKER);
	column_viewboxr(selected, (recti_12x16) {3, 8, 1, 8});

	// edbs-event
	selected = host.jobs = column_new();
	column_color(selected, color_cyan700);
	column_type(selected, ELM_WORKER);
	column_viewboxr(selected, (recti_12x16) {3, 0, 1, 8});


	// debuger
	debug_start();

	return 0;
}

int gman_serve() {
	glplotter_serve();
}

void gman_close() {
	glplotter_close();
}