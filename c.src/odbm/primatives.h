#ifndef graphic_primatives_h_
#define graphic_primatives_h_

#include <stdint.h>
#include <math.h>
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })


// note: can be cast too (GLfloat *)
#define vec3(prefix, type) typedef struct vec3##prefix{\
	union {                                   \
		type x;                                 \
		type red;                               \
	};                                        \
	union {                                   \
		type y;                                 \
		type green;                             \
	};                                        \
	union {                                   \
		type z;                                 \
		type blue;                              \
	};                                        \
} vec3##prefix;\
inline static vec3##prefix vec3##prefix##_add(vec3##prefix v, type a) \
{return (vec3##prefix){v.x+a,v.y+a,v.z+a};}; \
inline static vec3##prefix vec3##prefix##_sub(vec3##prefix v, type a) \
{return (vec3##prefix){v.x-a,v.y-a,v.z-a};}; \
inline static vec3##prefix vec3##prefix##_mul(vec3##prefix v, type a) \
{return (vec3##prefix){v.x*a,v.y*a,v.z*a};};

vec3(f,float)
vec3(i,int);
vec3(d,double );
vec3(ub,unsigned char);

// makes values that are 255 equal "1.000"
// and down to values that are 0 are "0".
static vec3d vec3ub_regulate(vec3ub a) {
	return (vec3d){
			.x = ((float)a.x/UINT8_MAX),
			.y = ((float)a.y/UINT8_MAX),
			.z = ((float)a.z/UINT8_MAX),
	};
}

// clamps all values over max to max.
static vec3d vec3d_clamp(vec3d v, double max) {
	v.x = min(v.x, max);
	v.y = min(v.y, max);
	v.z = min(v.z, max);
}

typedef struct {
	union {
		int width;
		int x;
	};
	union {
		int height;
		int y;
	};
} vec2i;

typedef struct {
	float x,y;
}vec2f;

typedef struct {
	float x,y,z,a;
} vec4f;
typedef struct {
	float r,g,b,a;
} colorf;

typedef struct {
	float x,y,width,heigth;
} rect_t;

typedef struct recti_t {
	int x,y,width,heigth;
} recti_t;

static void inline rect_growi(recti_t *r, int g) {
	r->x -= g;
	r->y -= g;
	r->width += g*2;
	r->heigth += g*2;
}

// converts the absolute position into relative area (returned)
static vec2i recti_relative(recti_t area, vec2i absolute) {
	vec2i ret;
	;

}

// retuns 1 if intersection, 0 otherwise
static int inline rect_intersectsi(recti_t a, recti_t b) {
	return (
			a.x < b.x + b.width &&
			a.x + a.width > b.x &&
			a.y < b.y + b.heigth &&
			a.y + a.heigth > b.y
	);
}

static int inline rect_contains(recti_t a, vec2i point) {
	return (
			a.y < point.y &&
			a.x < point.x &&
			a.y + a.heigth > point.y &&
			a.x + a.width  > point.x
	);
}

// will return the overlaping area in the two rectangles.
// will return 0 if their not overlaping, 1 otherwise
static int inline rect_overlapi(recti_t a, recti_t b, recti_t *o_overlap) {
	if(!rect_intersectsi(a,b)) {
		return 0;
	}

	// todo: I know for a fact this can be done without if statemnts.
	//       Some wierd combo of Math.min / Math.max Im sure.
	if (a.x < b.x) {
		o_overlap->x = b.x;
		o_overlap->width = min(b.width, a.width - (b.x - a.x));
	} else {
		o_overlap->x = a.x;
		o_overlap->width = min(a.width, b.width - (a.x - b.x));
	}
	if (a.y < b.y) {
		o_overlap->y = b.y;
		o_overlap->heigth = min(b.heigth, a.heigth - (b.y - a.y));
	} else {
		o_overlap->y = a.y;
		o_overlap->heigth = min(a.heigth, b.heigth - (a.y - b.y));
	}

	return 1;
}



#endif