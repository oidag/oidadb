#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <assert.h>
#include "../include/oidadb.h"
#include "odb-explorer.h"

#define inputbuffq  512
char inputbuffv[inputbuffq];
const char *filename;

const char *ontab(const char *strv, int charc) {
	if(!charc) {
		return "type \"help\" for commands";
	}

	return "not fucking clue.";
}
void setcommand(int up) {

}
void status(const char *status, int colpos) {
	dprintf(STDOUT_FILENO, "\n\r\033[K%s\r", status);
	dprintf(STDOUT_FILENO, "\033[A\033[%dC", colpos);
}

char readlinebuff[256];
int readline(const char *str) {
	assert(inputbuffq > 1); // needs space for 1 character plus null character
	dprintf(STDOUT_FILENO, "\033[37m%s\033[0m > ", str);
	int colpos = strlen(str) + 3;
	int i = 0;
	int dobreak = 0;
	char readchar;
	while(!dobreak) {
		ssize_t n = read(STDIN_FILENO, &readchar, 1);
		if(n == -1) {
			return -1;
		}
		switch(readchar) {
			case '\004':
				return -1;
			case '\n':
				dobreak=1;
				break;
			case '\t':
				status(ontab(inputbuffv, i),colpos);
				break;

			case 27: // escape

				// read the next char, make sure
				//read(STDIN_FILENO, &readchar, 1);
				break;
			case 0:
				break;

			case 127: // delete
				if(i == 0) break;
				dprintf(STDOUT_FILENO, "\33[D\33[K");
				colpos--;
				i--;
				break;

			default:
				// echo out the character and add it to the buffer
				write(STDOUT_FILENO, &readchar, 1);
				inputbuffv[i] = readchar;
				i++;
				colpos++;
				if(i == inputbuffq+1) {
					// we have no more room in the buffer, bail out.
					dobreak = 1;
				}
				break;
		}
	}
	inputbuffv[i] = 0;
	readchar = '\n';
	write(STDOUT_FILENO, &readchar, 1); // write a new line
	// kill any remaining buffer with the status
	dprintf(STDOUT_FILENO, "\033[K");
	return i;
}

int main(int argc, const char **argv) {
	/*if(argc == 1 || (argc == 2 && !strcmp(argv[1], "--help"))) {
		printf("usage: %s ODB-FILE\nJust run the thing... its a interactive terminal app.\n", argv[0]);
	}*/

	filename = "../../build/cmakedebug/edba_00.oidadb";
	setvbuf(stdout, NULL, _IONBF, 0);

	struct termios orignterm, newterm;
	tcgetattr(STDIN_FILENO, &orignterm);
	newterm = orignterm;

	newterm.c_lflag &= ~(ECHO | ICANON | ECHOE | ECHOK | ECHOCTL) ;
	tcsetattr(STDIN_FILENO, TCSANOW, &newterm);

	index_print();
	//page_print(65);
	//page_print(33);


	// set up the shell
	while(1) {
		//status("press tab at any time for help on whatever the hell you're doing.", 0);
		int n = readline("odb-explorer");
		if(n == -1) break;
		if(!strcmp(inputbuffv, "help")) {
			printf("Available commands: \n"
//				    "  open [FILE] open a oidadb file\n"
					"  index       show the entity index\n"
					"  page [PID]  describe the contents of page at PID\n"
					);
			continue;
		}
		if(!strcmp(inputbuffv, "index")) {
			index_print();
			continue;
		}
		if(!strncmp(inputbuffv, "page", sizeof("page")-1)) {
			odb_pid pid = 0;
			sscanf(inputbuffv, "page %ld", &pid);
			if(pid == 0) {
				printf("not valid page id\n");
				continue;
			}

			page_print(pid);
			continue;
		}
		printf("%s (%d) - unknown command\n", inputbuffv, n);
	}
	tcsetattr(STDIN_FILENO, TCSANOW, &orignterm);
}