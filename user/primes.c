#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void sieve(int pleft[2]) {
	int p;
	read(pleft[0], &p, sizeof(p));
	if(p == -1) {
		exit(0);
	}
	printf("prime %d\n", p);

	int pright[2];
	pipe(pright);
	if(fork() == 0) { // child
		close(pleft[0]);
		close(pright[1]); // close unnecessary fd (every forked process has a copy of the fd, so it's important to reduce the amount of it)
		sieve(pright);	
	} else { // parent
		close(pright[0]); // same as above
		int buf;
		while(read(pleft[0], &buf, sizeof(buf)) && buf != -1) {
			if(buf % p != 0) {
				write(pright[1], &buf, sizeof(buf));
			}
		}
		write(pright[1], &buf, sizeof(buf));
		// printf("e%d\n",p);
		close(pleft[0]); // same as above
		wait(0);
		exit(0);
	}
}

int main(int argc, char **argv) {
	int input_pipe[2];
	pipe(input_pipe);

	if(fork() == 0) { // child
		close(input_pipe[1]);
		sieve(input_pipe);
		exit(0);
	} else { // parent
		close(input_pipe[0]);
		int i;
		for(i=2;i<=35;i++){
			write(input_pipe[1], &i, sizeof(i));
		}
		i = -1;
		write(input_pipe[1], &i, sizeof(i));
	}
	wait(0);
	exit(0);
}