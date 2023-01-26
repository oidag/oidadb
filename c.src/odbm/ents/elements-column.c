#include <malloc.h>
#include <strings.h>
#include "elements_u.h"

typedef struct column_t {
	graphic_t *g;

	vec2i viewport_pos;
	vec2i viewport_size;

	color_t bgcolor;
	color_t fgcolor;

	element_type type;


} column_t;


static void viewport(graphic_t *g, eventdata_t e) {
	column_t *c = glp_userget(g);
	glp_viewport(g, (recti_t){
		c->viewport_pos.x,
		c->viewport_pos.y,
		c->viewport_size.width,
		c->viewport_pos.height,
	});
}
static void draw(graphic_t *g) {
	column_t *c = glp_userget(g);

	int width = c->viewport_size.width;
	int height = c->viewport_size.height;
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
	c->fgcolor = (color_t){1,1,1};
	c->viewport_size = (vec2i){100,100};
	c->viewport_pos  = (vec2i){100,100};

	return c;
}

// selects the column to be modifed by the next set of functions
void column_color(column_t *u, color_t col) {
	u->bgcolor = col;
}
void column_shard_color(column_t *u, color_t col) {
	u->fgcolor = col;
}

// x: from 0 to 11
// y: from 0 to 15
void column_posr(column_t *u, vec2i_12x16 pos) {
	u->viewport_pos = vec2i_12x16_real(pos,  glplotter_size());
	viewport(u->g, (eventdata_t){0});
}

void column_type(column_t *u, element_type type) {
	u->type = type;
}

void column_size(column_t *u, vec2i_12x16 size) {
	u->viewport_size = vec2i_12x16_real(size,  glplotter_size());
	viewport(u->g, (eventdata_t){0});
}