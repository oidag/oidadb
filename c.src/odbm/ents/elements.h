#ifndef elements_h_
#define elements_h_

typedef enum element_type {
	ELM_UNINIT, // unitialized (0)
	ELM_DESCRIPTOR,
	ELM_PAGE,
	ELM_WORKER,
	ELM_JOB,
	ELM_EVENT,
} element_type;

void element_host_start();

// todo: provide functions here to allow dbfile namespace to call whatever
//       it needs too.

#endif