#include "ents_u.h"

static void viewport(graphic_t *g, eventdata_t e);
static void draw(graphic_t *g);
static void events(graphic_t *g, eventdata_t e);

struct {
	int init;
} ent_dialog = {0};

// start
void ent_dialog_start() {
	if(ent_dialog.init) return; // singleton
	/*graphic_t *g = glp_new();
	glp_name(g, "NAME");
	viewport(g,(eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONWINDOWSIZE, viewport);*/
}