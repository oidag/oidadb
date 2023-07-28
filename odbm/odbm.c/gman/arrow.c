#include "gman_u.h"

#include <stdlib.h>

typedef struct arrow_t {
	graphic_t *g;

	// relative chords in the viewport.
	vec2i start,end;

	// never null:
	shard_t *s1, *s2;
} arrow_t;

const int linethickness = 3;
const int arrowhead_len = 5;

// extra height added by arrowhead
// Sense the arrow head is at a 45 degree angle, we multiply the length by
// sqrt(2) and round up.
const int extraheight = (int)((double)arrowhead_len * 1.414) + 1;

// private imports
extern void ent_shard_attachmentpoint(shard_t *s, vec2i *o_left, vec2i *o_right);


// ent_*_new: means you can call it multiple times and create new things each time
// ent_*_start: singleton. only one can exist at a time.
// template:

static void viewport(graphic_t *g, eventdata_t e) {
	arrow_t *ent = glp_userget(g);

	// find out which one is furthest left
	vec2i start,end,a,b,c,d;
	ent_shard_attachmentpoint(ent->s1, &a, &b);
	ent_shard_attachmentpoint(ent->s2, &c, &d);
	if(a.x > c.x) {
		// s1 is further on the right
		start = a;
		end = d;
	} else {
		// s2 is futher on the right
		start = b;
		end = c;
	}

	glp_viewport_t vp = (glp_viewport_t){
			.x = start.x,
			.y = start.y - extraheight,
			.width = abs(end.x - start.x),
			.heigth = abs(end.y - start.y) + extraheight*2,
	};
	glp_viewport(g, vp);

	ent->start.x = start.x - vp.x;
	ent->start.y = start.y - vp.y;
	ent->end.x = end.x - vp.x;
	ent->end.y = end.y - vp.y;

}
static void draw(graphic_t *g) {
	recti_t v = glp_viewportget(g);
	arrow_t *u = glp_userget(g);


	// arrow

	int median = v.width/2;

	// the line
	glPushMatrix();
	glLineWidth(linethickness);
	glBegin(GL_LINES);
	color_glset(color_violet100);
	glVertex2i(u->start.x,u->start.y);
	glVertex2i(median,u->start.y);

	glVertex2i(median,u->start.y);
	glVertex2i(median,u->end.y);

	glVertex2i(median,u->end.y);
	glVertex2i(u->end.x,u->end.y);
	glEnd();
	glPopMatrix();


}
static void events(graphic_t *g, eventdata_t e);

static void ondelete(graphic_t *g) {
	shard_t *u = glp_userget(g);
	free(u);
	glp_user(g,0,0);
}

// start
arrow_t *ent_arrow_new(shard_t *s1, shard_t *s2) {
	graphic_t *g = glp_new();
	glp_name(g, "arrow");

	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONWINDOWSIZE, viewport);
	// mallocs
	arrow_t *u = malloc(sizeof(arrow_t));
	u->g = g;
	u->s1 = s1;
	u->s2 = s2;

	glp_user(u->g, u, ondelete);
	viewport(g,(eventdata_t){0});

	return u;
}