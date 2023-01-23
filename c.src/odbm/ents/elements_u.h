#ifndef elements_h_
#define elements_h_
#include "ents_u.h"

typedef enum ent_type {
	ENT_UNINIT, // unitialized (0)
	ENT_DESCRIPTOR,
	ENT_PAGE,
	ENT_WORKER,
	ENT_JOB,
	ENT_EVENT,
} ent_type;

color_t static ent_type2color(ent_type t) {
	switch (t) {
		default:
			return  color_pink600;
		case ENT_DESCRIPTOR:
			return color_violet400;
		case ENT_PAGE:
			return color_pink900;
		case ENT_WORKER:
			return color_emerald400;
		case ENT_JOB:
			return color_cyan700;
		case ENT_EVENT:
			return color_slate100;
	}
}
static const char * ent_type2str(ent_type t) {
	switch (t) {
		default:
			return  "UNKNWON";
		case ENT_DESCRIPTOR:
			return "DESC";
		case ENT_PAGE:
			return "PAGE";
		case ENT_WORKER:
			return "WORKER";
		case ENT_JOB:
			return "JOB";
		case ENT_EVENT:
			return "EVENT";
	}
}

#endif