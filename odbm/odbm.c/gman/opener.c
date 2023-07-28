#include "gman_u.h"


typedef struct {
	const char *errmsg;

	// 0 for not opened
	// 1 for opened
	// 2 for open error
	int state;

	void (*onload)();

} ent_opener_t;

ent_opener_t opener; // todo

// pixels in width and height
static int width = 500;
static int height = 200;


static void event(graphic_t *g, eventdata_t e) {
	if (e.keyboard.key != GLP_KEY_R) return;
	if (opener.state != 1) {
		int err = file_init("testfile/test.oidadb");
		if (err) {
			opener.state = 2;
			opener.errmsg = "Failed to open.";
		} else {
			opener.onload();
			opener.state = 1;
			glp_destroy(g);
		}
		glp_invalidate(g);
	}
}


static void draw(graphic_t *g) {
	if(opener.state != 1) {
		glColor3f(1, 0, 0);
		text_setfont(text_defaults_h1());
		if(opener.state == 2) {
			text_draw((float)width/2 - text_width(opener.errmsg)/2, 70, opener.errmsg);
		}
		const char *message = "Press 'R' to retry.";
		text_draw((float)width/2 - text_width(message)/2, 32, message);
	}

}

static void setviewport(graphic_t *g, eventdata_t _) {
	// move it to the middle of the screen
	vec2i size = glplotter_size();
	glp_viewport(g, (recti_t) {
			size.width / 2 - (width/2),
			             size.height / 2 - (height/2),
			             width,
			             height,
	             }
	);
}

void ent_opener_new(void (*onload)()) {
	graphic_t *g = glp_new();
	opener.state = 0;
	opener.onload = onload;
	glp_user(g, &opener, 0);
	setviewport(g, (eventdata_t){0});
	glp_name(g, "opener");
	glp_draw(g, GLP_SLEEPER, draw);
	glp_events(g, DAF_ONKEYDOWN, event);
	glp_events(g, DAF_ONWINDOWSIZE, setviewport);
}