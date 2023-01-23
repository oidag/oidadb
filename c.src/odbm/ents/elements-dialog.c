#include "elements_u.h"


struct {
	int init;

	ent_type type;

} ent_dialog = {0};

// generates the header (ie: pid#AA02)
static const char * specificheader() {
	return "pid#AA20"; // todo
}

static void viewport(graphic_t *g, eventdata_t e)
{
	vec2i size = glplotter_size();
	int w = size.width/12*7;
	int h = size.height/8*5;
	recti_t v = {
			size.width - w - (size.width/12/2),
			size.height/8*2 + size.height/8/2,
			w,
			h,
	};
	glp_viewport(g, v);
}
static void draw(graphic_t *g) {

	glp_viewport_t vp = glp_viewportget(g);

	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,1,0,1,1,-1);

	// bg
	glBegin(GL_QUADS);
	color_glset(color_slate700);
	glVertex2f(0,0);
	glVertex2f(0,1);
	glVertex2f(1,1);
	glVertex2f(1,0);
	glEnd();
	glPopMatrix();




	// header
	int h1width = vp.width;
	int h1heigth = vp.heigth/24*3;
	glViewport(vp.x,
			   vp.y + vp.heigth - h1heigth,
			   h1width,
			   h1heigth);

	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,1,0,1,1,-1);
	glBegin(GL_QUADS);
	color_glset(ent_type2color(ent_dialog.type));
	glVertex2f(0,0);
	glVertex2f(0,1);
	glVertex2f(1,1);
	glVertex2f(1,0);
	glEnd();
	glPopMatrix();


	glPushMatrix();
	glLoadIdentity();
	glOrtho(0,h1width,0,h1heigth,1,-1);

	// h1
	text_defaults_h1();
	float textwidth = text_width("W");
	float y = (float)h1heigth/2 - text_height("W")/2;
	color_glset(color_slate50);

	// specific h1
	text_draw(textwidth, y, specificheader());
	// general h1
	const char *str = ent_type2str(ent_dialog.type);
	text_draw((float)h1width - text_width(str) - textwidth, y, str);
	glPopMatrix();






}
static void events(graphic_t *g, eventdata_t e);

// start
void ent_dialog_start() {
	if(ent_dialog.init) return; // singleton
	ent_dialog.init = 1;
	ent_dialog.type = ENT_PAGE;
	graphic_t *g = glp_new();
	glp_name(g, "element-dialog");
	viewport(g,(eventdata_t){0});
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONWINDOWSIZE, viewport);
}