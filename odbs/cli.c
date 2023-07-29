#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#include "cli.h"
#include "assert.h"

#define inputbuffq  512
char inputbuffv[inputbuffq];

struct {
	int fd;
	const char *filename;
	void *loadedpage; // used for odbfile_page
} odbfile = {0};

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

struct cmd_arg cmd_arg;


int help();



// returns 0 if pointer is not arg, if convert is supplied then will return 0 if failed
// to parse.
// if `in` is null: returuns const char *
// if `is` is not null: returns uint64_t of bytes read from in
const char *getargename(void *arg, const char *convert) {
	if(arg == &cmd_arg.pid) {
		if (convert) {
			if(sscanf(convert, "%ld", arg) != 1) return 0;
		}
		return "PID";
	}
	if(arg == &cmd_arg.offset) {
		if (convert) {
			if(sscanf(convert, "%ld", arg) != 1) return 0;
		}
		return "OFF";
	}
	if(arg == &cmd_arg.file) {
		if (convert) {
			memcpy(arg, convert, strlen(convert));
		}
		return "FILE";
	}
	return 0;
}
int parseargs(struct cmd cmd, const char *in) {
	int n;
	const char *argstarts[maxargs] = {0};
	int arglen[maxargs] = {0};
	assert(cmd.argc <= maxargs);

	int argc = 0;
	int inquotes = 0;
	while(*in != 0) {
		if(*in == ' ') {in++;continue;} // skip
		if(*in == '"') {inquotes = 1;in++;}
		// start of a new arg
		argstarts[argc] = in;
		// skip all normal characters until its whitespace
		do {
			if(inquotes && *in == '"') {
				inquotes = 0;
				in++;
				continue;
			} else {
				arglen[argc]++;
				in++;
			}
		} while(*in && (inquotes || *in != ' '));
		if(arglen[argc]) // don't add args with 0 len.
			argc++;
	}
	if(argc != cmd.argc) {
		printf("incorrect amount of arguments, expected %d, got %d\n", cmd.argc, argc);
		return 1;
	}

	char inbuff[255];
	for(int i = 0; i < argc; i++) {
		memcpy(inbuff, argstarts[i], arglen[i]);
		inbuff[arglen[i]] = 0;
		if(!getargename(cmd.argv[i], inbuff)) {
			printf("failed to parse arg %d (%s)\n", i, getargename(cmd.argv[i], 0));
			return 1;
		}
	}
	return 0;
}
void printargs( struct cmd cmd, char *buff) {
	int n;
	char *nbuff = buff;
	for(int i = 0; i < cmd.argc; i++) {
		n = sprintf(nbuff, " [%s]", getargename(cmd.argv[i], 0));
		nbuff = (void *)nbuff + n;
	}
}

int help() {
	printf("Available commands: \n");
	char printbuff[255];
	for(int i = 0; i < cmdc; i++) {
		int n = sprintf(printbuff, "%s", cmds[i].command);
		printargs(cmds[i], &printbuff[n]);
		printf("  %-23s %s\n", printbuff, cmds[i].description);
	}
}

void shell_loop() {
	setvbuf(stdout, NULL, _IONBF, 0);

	struct termios orignterm, newterm;
	tcgetattr(STDIN_FILENO, &orignterm);
	newterm = orignterm;

	newterm.c_lflag &= ~(ECHO | ICANON | ECHOE | ECHOK | ECHOCTL) ;
	tcsetattr(STDIN_FILENO, TCSANOW, &newterm);
	while(1) {
		//status("press tab at any time for help on whatever the hell you're doing.", 0);
		int n = readline("odbs");
		if(n == -1) break;

		int i;
		for(i = 0; i < cmdc; i++) {
			assert(strlen(cmds[i].command) < inputbuffq);
			if( !strncmp(cmds[i].command, inputbuffv, strlen(cmds[i].command)) ) {
				break;
			}
		}
		if(i == cmdc) {
			printf("%s (%d) - unknown command\n", inputbuffv, n);
			continue;
		}
		if(!parseargs(cmds[i], (void *)inputbuffv + strlen(cmds[i].command))) {
			cmds[i].func();
		}
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &orignterm);
}