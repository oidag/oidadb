
#include <drawtext.h>
#include <GL/gl.h>

#include "error.h"
#include "text.h"

const char *monospace_f = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
const char *sans_f = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
const char *serif_f = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf";
static text_font monospace ={0},body={0},headying={0};

text_font text_defaults_monospace() {
	if(!monospace.font) {
		text_addfont(monospace_f, 16, &monospace);
	}
	return monospace;
}

text_font text_defaults_body() {
	if(!body.font) {
		text_addfont(serif_f, 16, &body);
	}
	return body;
}
text_font text_defaults_h1() {
	if(!headying.font) {
		text_addfont(sans_f, 32, &headying);
	}
	return headying;
}

void text_setfont(text_font font) {
	dtx_use_font(font.font, font.size);
}
int text_addfont(const char *file, int size, text_font *o_font) {
	o_font->size = size;
	o_font->font = dtx_open_font(file, size);
	if(!o_font->font) {
		error("open font");
		return 1;
	}
	return 0;
}
float text_width(const char *text) {
	return dtx_string_width(text);
}
void text_draw(float x, float y, const char *text) {
	glPushMatrix();
	//glTranslatef(x,y + (dtx_string_height(text) - dtx_line_height()),0);
	glTranslatef(x,y,0);
	dtx_string(text);
	glPopMatrix();
}