#ifndef elementsu_h_
#define elementsu_h_
#include "ents_u.h"

color_t static element_type2color(element_type t) {
	switch (t) {
		default:
			return  color_pink600;
		case ELM_DESCRIPTOR:
			return color_violet400;
		case ELM_PAGE:
			return color_pink900;
		case ELM_WORKER:
			return color_emerald400;
		case ELM_JOB:
			return color_cyan700;
		case ELM_EVENT:
			return color_slate100;
	}
}
static const char * element_type2str(element_type t) {
	switch (t) {
		default:
			return  "UNKNWON";
		case ELM_DESCRIPTOR:
			return "DESC";
		case ELM_PAGE:
			return "PAGE";
		case ELM_WORKER:
			return "WORKER";
		case ELM_JOB:
			return "JOB";
		case ELM_EVENT:
			return "EVENT";
	}
}

typedef struct shard_t shard_t;
typedef struct column_t column_t;

// children of column_t
// hmmmmmm... maybe don't need these... too much politic code.
/*typedef struct     column_edbd_t column_edbd_t;
column_edbd_t     *column_edbd_new();
typedef struct     column_pages_t column_pages_t;
column_pages_t    *column_pages_new();
typedef struct     column_pagesbuf_t column_pagesbuf_t;
column_pagesbuf_t *column_pagesbuf_new();*/

column_t *column_new();

// selects the column to be modifed by the next set of functions
void column_color(column_t *, color_t);
void column_shard_color(column_t *, color_t);

// x: from 0 to 11
// y: from 0 to 15
void column_pos(column_t *, int x, int y);

void column_type(column_t *, element_type type);

// height should be from 1 (min) and 12 (screen width)
void column_width(column_t *, int);

// height should be from 1 (min) and 16 (screen height)
void column_height(column_t *, int);


// used for deriving classes
//  - When selected, the dialog will be notified and it will set the contents
//
// The column determians a lot of the shards behaviour such as bgcolor,
// selected animation, drawing arrows, position, ect.
//
// the shard itself can be drawn. other than its background color
shard_t *shard_new(column_t *owner);
void shard_cookie(shard_t *, void *cookie);

// can be set to put aditional shit on the shard
void shard_ondraw(shard_t *, void (*)cb(void *cookie));

// adds an arrow from the src shard to the dest shard.
void shard_point(shard_t *src, shard_t *dest);



#endif