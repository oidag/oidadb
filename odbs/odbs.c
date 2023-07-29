#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <assert.h>
#include <oidadb/oidadb.h>
#include "odbs.h"

int openodb() {
	printf("opening %s...\n", cmd_arg.file);
}


struct cmd cmds[] = {
		{
				.command     = "help",
				.description = "show this dialog",
				.func        = help,
				.argc        = 0,
		},
		{
				.command     = "open",
				.description = "open (or switch to) an oidadb file",
				.func        = openodb,
				.argc        = 1,
				.argv        = {&cmd_arg.file},
		},
		{
				.command     = "index",
				.description = "show the entity index",
				.func        = index_print,
				.argc        = 0,
				.argv        = 0,
		},
		{
				.command     = "page",
				.description = "describe the contents of page at PID",
				.func        = page_print,
				.argc        = 1,
				.argv        = {&cmd_arg.pid},
		},
		{
				.command     = "obj",
				.description = "print an object found on PID at object offset OFF",
				.func        = print_obj,
				.argc        = 2,
				.argv        = {&cmd_arg.pid, &cmd_arg.offset},
		},
		{
				.command     = "btree",
				.description = "print out the btree starting at lookup page PID",
				.func        = print_btree,
				.argc        = 1,
				.argv        = {&cmd_arg.pid},
		}
};

int cmdc = sizeof(cmds)/sizeof(cmds[0]);

int main(int argc, const char **argv) {
	if(argc == 2 && !strcmp(argv[1], "--help")) {
		printf("usage: %s FILE\nJust run the thing... its a interactive terminal app.\n", argv[0]);
		return 0;
	}

	setvbuf(stdout, NULL, _IONBF, 0);

	struct termios orignterm, newterm;
	tcgetattr(STDIN_FILENO, &orignterm);
	newterm = orignterm;

	newterm.c_lflag &= ~(ECHO | ICANON | ECHOE | ECHOK | ECHOCTL) ;
	tcsetattr(STDIN_FILENO, TCSANOW, &newterm);



	// set up the shell
	shell_loop();
	tcsetattr(STDIN_FILENO, TCSANOW, &orignterm);
}

const odb_spec_struct_struct *odbfile_stk(odb_sid sid){} // returns 0 on error/eof
const odb_spec_index_entry   *odbfile_ent(odb_eid eid){} // returns 0 on error/eof
const odb_spec_head          *odbfile_head(){}
const void *odbfile_page(odb_pid){}
unsigned int odbfile_pagesize(){}
