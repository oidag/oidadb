#ifndef _HANDLE_H_
#define _HANDLE_H_ 1

typedef struct edbh_st {

	pid_t hostpid;

} edbh;

typedef struct {

	int clientc; // count of clients, when 0: the host will shut down.

} edb_host;


#endif