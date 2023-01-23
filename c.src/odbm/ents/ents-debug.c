#include <GL/gl.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include "ents.h"
#include "../text.h"
#include "../glplotter/glplotter.h"

static const char *vals[] = {
"DAF_ONMOUSE_DOWN",
"DAF_ONMOUSE_UP",
"DAF_ONMOUSE_MOVE",
"DAF_ONSCROLL",
"DAF_ONKEYDOWN",
"DAF_ONKEYREPEAT",
"DAF_ONKEYUP",
"DAF_ONWINDOWSIZE",
"(none)",
};

static void setviewport(graphic_t *g) {
	vec2i size = glplotter_size();
	ent_debug_t *d = glp_userget(g);

	const int margin = 40;

	glp_viewport(g, (glp_viewport_t){
			size.width - d->width - margin,
			size.height - d->height - margin,
			d->width,
			d->height,
	});
}

static void onevent(graphic_t *g, eventdata_t e) {
	if(e.type == DAF_ONWINDOWSIZE)
		setviewport(g);
	ent_debug_t *d = glp_userget(g);
	d->lastevent = e.type;
}

static void draw(graphic_t *g){
	ent_debug_t *d = glp_userget(g);
	int w = d->width;
	int h = d->height;
	d->frameid++;

	struct timeval tv;
	gettimeofday(&tv, 0);
	if(tv.tv_sec > d->frame_last_rec) {
		time_t secs = tv.tv_sec - d->frame_last_rec;
		unsigned int totalframes = d->frameid - d->frame_last_recid;
		d->fps = totalframes / (unsigned int)secs;

		d->frame_last_rec = tv.tv_sec;
		d->frame_last_recid = d->frameid;
	}

	glColor3f(0,1,0);
	text_setfont(d->font);
	snprintf(d->buff, sizeof(d->buff),
			 "Frame: %x\nLast Event: %s\nFPS: %d",
			d->frameid,
			vals[d->lastevent],
			d->fps);
	text_draw(8,24+8+8, d->buff);
}



void ent_debug_new(ent_debug_t *o_ent) {
	graphic_t *g = glp_new();

	bzero(o_ent, sizeof(ent_debug_t));
	o_ent->font = text_defaults_monospace();
	o_ent->lastevent = 8;
	o_ent->height = 100;
	o_ent->width = 300;

	glp_user(g, o_ent, 0);
	setviewport(g);
	glp_name(g, "debugger");
	glp_draw(g, GLP_INVALIDATED, draw);
	glp_events(g, DAF_ALL, onevent);
}