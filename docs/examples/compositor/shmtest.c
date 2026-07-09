/* shmtest.c - verify SysV shm cross-process attach-by-shmid on MINIX.
 * Child creates a segment and writes a pattern; parent independently
 * shmat()s the same shmid and checks it.  Prints SHM-OK or SHM-FAIL. */
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>

int
main(void)
{
	int pipefd[2], id, i, ok = 1;
	uint32_t *p;

	if (pipe(pipefd) < 0) { printf("SHM-FAIL pipe\n"); return 1; }
	if (fork() == 0) {			/* child = creator */
		id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
		if (id < 0) { printf("SHM-FAIL child shmget\n"); _exit(1); }
		p = shmat(id, NULL, 0);
		if (p == (void *)-1) { printf("SHM-FAIL child shmat\n"); _exit(1); }
		for (i = 0; i < 1024; i++) p[i] = 0xA5000000u + i;
		write(pipefd[1], &id, sizeof id);
		sleep(3);			/* keep it alive */
		_exit(0);
	}
	read(pipefd[0], &id, sizeof id);	/* parent = independent reader */
	p = shmat(id, NULL, SHM_RDONLY);
	if (p == (void *)-1) { printf("SHM-FAIL parent shmat\n"); return 1; }
	for (i = 0; i < 1024; i++)
		if (p[i] != 0xA5000000u + (uint32_t)i) { ok = 0; break; }
	printf(ok ? "SHM-OK cross-process attach works\n"
		  : "SHM-FAIL data mismatch at %d\n", i);
	wait(NULL);
	return ok ? 0 : 1;
}
