#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "file.h"
#include "error.h"
typedef struct {
	int fd;
} file;

file f;
int file_getpagesize() {
	return 4096;
}
int file_init(const char *file) {
	f.fd = open(file, O_RDONLY);
	if(f.fd == -1) {
		error("open");
		return 1;
	}
	return 0;
}

int getpagec() {
	struct stat s;
	int err = fstat(f.fd, &s);
	if(err == -1) {
		error("stat");
		return 0;
	}
	return (int)(s.st_size / file_getpagesize())+1;
}

void file_close() {
	close(f.fd);
}


