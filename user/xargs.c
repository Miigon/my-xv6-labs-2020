#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void run(char *program, char **args) {
	// for(int i=0;i<argc; i++) {
	// 	printf("i: %s\n", args[i]);
	// }
	if(fork() == 0) { // child exec
		exec(program, args);
		exit(0);
	}
	return; // parent return
}

int main(int argc, char *argv[]){
	char buf[2048]; char *p = buf, *last_p = buf;
	char *argsbuf[128]; char **args = argsbuf;
	for(int i=1;i<argc;i++) {
		*args = argv[i];
		args++;
	}
	char **pa = args;
	while(read(0, p, 1) != 0) {
		if(*p == ' ') {
			*p = '\0';
			*(pa++) = last_p; // save argument
			last_p = p+1;
		} else if (*p == '\n') {
			*p = '\0';
			*(pa++) = last_p;
			last_p = p+1;
			*pa = 0; // null-terminated list
			run(argv[1], argsbuf);
			pa = args; // reset
		}
		p++;
	}
	if(pa != args) { // has one line left
		*p = '\0';
		*(pa++) = last_p;
		*pa = 0; // null-terminated list
		run(argv[1], argsbuf);
	}
	while(wait(0) != -1) {};
	exit(0);
}
