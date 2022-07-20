#ifndef _HANDLE_H_
#define _HANDLE_H_ 1

typedef struct edbh_st {

	// depending on the handle, either host xor client will be not-null
	client *edb_client;

} edbh;

typedef struct {

} edb_host;


#endif