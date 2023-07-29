#ifndef odbs_cli_h_
#define odbs_cli_h_

#include <oidadb/oidadb.h>

#define maxargs 8


struct cmd {
	const char *command;
	const char *description;

	int (*func)();

	// a pointer array. Each point must point to a field inside of cmd_arg.
	// IE: argv = {&cmd_arg.file, &cmd_arg.offset}; argc=2
	int    argc;
	void  *argv[maxargs];
};

struct cmd_arg {
	char        file[255];
	odb_pid     pid;
	uint32_t    offset;
};
extern struct cmd_arg cmd_arg;

// defined in odbs.c
extern struct cmd cmds[];
extern int cmdc;

int help();
void shell_loop();
#endif