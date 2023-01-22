#ifndef text_h_
#define text_h_

typedef struct {
	struct dtx_font *font;
	int size;
} text_font;

text_font text_defaults_monospace();
text_font text_defaults_body();
text_font text_defaults_h1();

int text_addfont(const char *file, int size, text_font *o_font);

void text_setfont(text_font font);
float text_width(const char *text);
void text_draw(float x, float y, const char *text);

#endif