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
float text_height(const char *text);
void text_draw(float x, float y, const char *text);

// only writes up to size chars found in text.
// if size is -1, will look for null term.
void text_drawc(float x, float y, const char *text, int size);

#endif