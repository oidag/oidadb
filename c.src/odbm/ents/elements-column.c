#include <malloc.h>
#include <strings.h>
#include "elements_u.h"

#define SHARD_INC 64;

#define SHARD_V_MARGIN 4
#define SHARD_H_MARGIN 4

typedef struct column_t {
	graphic_t *g;

	recti_12x16 viewbox_setting;
	recti_t viewport;

	// removing is done by index shifting. So don't try to
	// keep anything pointing to any array index because
	// its value will change.
	graphic_t **shardv;
	int         shardc;
	int         shardq;

	color_t bgcolor;

	element_type type;


} column_t;


// assumes the parent's viewport is already set.
void static shard_vp(column_t *u, int index) {
	graphic_t *shard = u->shardv[index];

	// calculate the "base viewport"... one without margins.
	int shardheight = u->viewport.heigth / u->shardc;
	recti_t vp = (recti_t){u->viewport.x,
	                       u->viewport.y + u->viewport.heigth - shardheight*(index+1),
			               u->viewport.width,
			               shardheight};

	// now add margin
	vp.y += SHARD_V_MARGIN*(index+1);
	vp.x += SHARD_H_MARGIN;
	vp.width -= SHARD_H_MARGIN * 2;

	glp_viewport(shard,vp);
}

static void viewport(graphic_t *g, eventdata_t e) {
	column_t *c = glp_userget(g);
	c->viewport = recti_12x16_real(c->viewbox_setting,  glplotter_size());
	glp_viewport(g, c->viewport);

	// set up children viewport
	for(int i = 0; i < c->shardc; i++) {
		shard_vp(c, i);
	}
}
static void draw(graphic_t *g) {
	column_t *c = glp_userget(g);

	int width = c->viewport.width;
	int height = c->viewport.heigth;
	// background
	glBegin(GL_QUADS);
	color_glset(c->bgcolor);
	glVertex2i(0,0);
	glVertex2i(0,height);
	glVertex2i(width,height);
	glVertex2i(width,0);
	glEnd();
	// shards are drawn on top of this.
}
static void ondelete(graphic_t *g) {
	column_t *c = glp_userget(g);
	// free the children
	for(int i = 0; i < c->shardc; i++) {
		glp_destroy(c->shardv[i]);
	}
	if(c->shardv) free(c->shardv);
	free(c);
}

column_t *column_new() {
	column_t *c = malloc(sizeof(column_t));
	bzero(c, sizeof(column_t));
	graphic_t *g = glp_new();
	c->g = g;
	glp_user(g, c, ondelete);
	glp_name(g, "column");
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONWINDOWSIZE, viewport);

	// some (ugly) default values to make remind me
	// to use other functions.
	c->bgcolor = (color_t){1,0,0};
	c->viewport = (recti_t){100,100,100,100};

	return c;
}

// selects the column to be modifed by the next set of functions
void column_color(column_t *u, color_t col) {
	u->bgcolor = col;
}

// x: from 0 to 11
// y: from 0 to 15
void column_viewboxr(column_t *u, recti_12x16 r) {
	u->viewbox_setting = r;
	viewport(u->g, (eventdata_t){0});
}

void column_type(column_t *u, element_type type) {
	u->type = type;
}

// only accessable in shard class.
//
// takes care of viewport.
//
// takes care of resizing.
//
// todo: threading with deletion/access methods
void column_place_child(column_t *u, graphic_t *shard) {

	if(u->shardc == u->shardq) {
		// out of space. Add some more.
		u->shardq+=SHARD_INC;
		u->shardv = realloc(u->shardv, u->shardq * sizeof(graphic_t *));
	}

	u->shardv[u->shardc] = shard;
	// set up its viewport.
	shard_vp(u,u->shardc);
	u->shardc++;

	// We don't need to take care of the ONWINDOWSIZE to update the
	// viewport because the parent column will invoke glp_viewport on all
	// child shards.
	//glp_events(u->g, DAF_ONWINDOWSIZE, );
}
element_type column_typeget(column_t *u) {
	return u->type;
}