#ifndef ENTS_H_
#define ENTS_H_

#include "text/text.h"

#include <bits/types/time_t.h>

typedef enum element_type {
	ELM_UNINIT, // unitialized (0)
	ELM_DESCRIPTOR,
	ELM_PAGE,
	ELM_WORKER,
	ELM_JOB,
	ELM_EVENT,
} element_type;

int gman_init();
int gman_serve();
void gman_close();

// todo: provide functions here to allow dbfile namespace to call whatever
//       it needs too.

typedef struct ent_debug_t ent_debug_t;

void debug_start();

typedef struct ent_background_t {
	int _;
} ent_background_t;

int background_start();

void ent_opener_new(void (*onload)());

// abducts stderr and stdout
void ent_terminal_start();


#endif