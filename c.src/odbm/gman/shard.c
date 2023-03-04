#include <strings.h>
#include <malloc.h>
#include "gman_u.h"
//#include "../../../include/ellemdb.h"

// defined in column. Cross-defined here.
extern void column_place_child(column_t *u, graphic_t *shard);
extern element_type column_typeget(column_t *u);

const int bordersize = 3;

typedef struct shard_t {
	int isselected;
	int ishover;
	column_t *owner;
	graphic_t *g;
	void *cookie;
	void (*drawselector)(void *cookie);
} shard_t;


// exported to arrow.c
// returns pixel location of the left and right (respectively) attachment point
// for the arrow.
void ent_shard_attachmentpoint(shard_t *s, vec2i *o_left, vec2i *o_right) {
	glp_viewport_t v = glp_viewportget(s->g);

	int y = v.y + v.heigth / 2;

	o_left->x = v.x + bordersize;
	o_right->x = v.x + v.width - bordersize;

	o_left->y = y;
	o_right->y = y;
}

static void draw(graphic_t *g) {
	shard_t *ent = glp_userget(g);

	color_t bg_norm = element_type2color(column_typeget(ent->owner));

	// calculate the hover color
	vec3d bg_hover = vec3ub_regulate(bg_norm);
	if(color_luminance(bg_norm) > 0.5) {
		// bg_norm is normally bright... remove color for the hover.
		bg_hover = vec3d_mul(bg_hover, 0.8);
	} else {
		// bg_norm is normall dark
		bg_hover = vec3d_mul(bg_hover, 1.2);
		bg_hover = vec3d_clamp(bg_hover, 1);
	}

	// special border (if selected)
	if(!ent->isselected) {
		glp_draw(ent->g, GLP_SLEEPER, draw);
	} else {
		glp_draw(ent->g, GLP_ANIMATE, draw);

		// now to draw some magical shit. You can try to
		// read through this but who cares. If it's bad
		// then just make another.
		// constantly ticking up from 0 to 1.
		double c50 = (double)(glplotter_frameid % 110) / 110;
		double c = (double)(glplotter_frameid % 80) / 80;
		double t = sin( 3.1415*c);

		glPushMatrix();
		glLoadIdentity();
		glOrtho(0,1,0,1,1,-1);
		glBegin(GL_QUADS);
		color_glset(color_violet900);
		glVertex2f(0,0);
		glVertex2f(0,1);
		glVertex2f(1,1);
		glVertex2f(1,0);

		glColor4d(((double )color_pink400.red/(double )UINT8_MAX),
		          ((double )color_pink400.green/(double )UINT8_MAX),
		          ((double )color_pink400.blue/(double )UINT8_MAX),
				  t);
		glVertex2f(0,0);
		glVertex2f(0,1);
		glVertex2f(1,1);
		glVertex2f(1,0);
		glEnd();

		c = c50;
		c = c * 1.6;
		c -= 0.6;
		vec3ub scolor = color_slate50;
		glBegin(GL_QUADS);
		//start
		glColor4ub(scolor.red,
		           scolor.green,
		           scolor.blue,
		           0);
		glVertex2d(0+c,0);
		glVertex2d(0.2+c,1);
		glColor4ub(scolor.red,
		           scolor.green,
		           scolor.blue,
		           255);
		glVertex2d(0.4+c,1);
		glVertex2d(0.2+c,0);
		//end block
		glColor4ub(scolor.red,
		           scolor.green,
		           scolor.blue,
		           255);
		glVertex2d(0.2+c,0);
		glVertex2d(0.4+c,1);
		glColor4ub(scolor.red,
		           scolor.green,
		           scolor.blue,
		           0);
		glVertex2d(0.6+c,1);
		glVertex2d(0.4+c,0);
		glEnd();

		glPopMatrix();
	}

	// background
	// add margin to background (for border)
	recti_t v = glp_viewportget(g);
	rect_growi(&v, -bordersize);
	glViewport(v.x, v.y, v.width, v.heigth);
	if(ent->ishover) {
		glColor3dv((GLdouble *)&bg_hover);
	} else {
		color_glset(bg_norm);
	}
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,1,0,1,1,-1);
	glBegin(GL_QUADS);
	glVertex2f(0,0);
	glVertex2f(0,1);
	glVertex2f(1,1);
	glVertex2f(1,0);
	glEnd();
	glPopMatrix();

	// alright background/seleection animation set.
	// let the child do the rest
	if(ent->drawselector) {
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0,v.width,0,v.heigth,1,-1);
		ent->drawselector(ent->cookie);
		glPopMatrix();
	}
}

static void event(graphic_t *g, eventdata_t e) {
	shard_t *ent = glp_userget(g);
	switch (e.type) {
		case DAF_ONMOUSE_DOWN:
			if(ent->ishover) {
				ent->isselected = !ent->isselected;
			}
			glp_invalidate(g);
			break;
		default:
		case DAF_ONMOUSE_MOVE:
			ent->ishover = rect_contains(glp_viewportget(g), e.pos);
			glp_invalidate(g);
			break;
	}
}

static void ondelete(graphic_t *g) {
	shard_t *u = glp_userget(g);
	free(u);
	glp_user(g,0,0);
}


void shard_cookie(shard_t *s, void *cookie) {
	s->cookie = cookie;
}

// can be set to put aditional shit on the shard
void shard_ondraw(shard_t *s, void (*cb)(void *cookie)) {
	s->drawselector = cb;
}

// start
shard_t *shard_new(column_t *owner) {

	shard_t *u = malloc(sizeof(shard_t));
	bzero(u, sizeof(shard_t ));
	u->owner = owner;

	u->g = glp_new();
	glp_name(u->g, "shard");
	glp_user(u->g, u, ondelete);

	// take care of the placement.
	column_place_child(owner, u->g);

	glp_draw(u->g, GLP_SLEEPER, draw);
	glp_events(u->g, DAF_ONMOUSE_MOVE, event);
	glp_events(u->g, DAF_ONMOUSE_DOWN, event);

	return u;
}

