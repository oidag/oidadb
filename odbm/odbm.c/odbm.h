#ifndef file_h_
#define file_h_

#include "gman/gman.h"

#include "error.h"

int file_getpagesize();
int file_init(const char *file);
int file_getpagec();
void file_close();

#endif