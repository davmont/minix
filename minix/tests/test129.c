/* Test 129 - select(2) and poll(2) at scale, and poll's POLLNVAL.
 *
 * Many processes can wait in select() or poll() at the same time (there
 * used to be room for 25 in all the system), and poll() reports descriptors
 * that are not open with POLLNVAL rather than failing.
 */
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define WAITERS	40

static void
test_many_waiters(void)
{
	int pipes[WAITERS][2];
	pid_t pids[WAITERS];
	int i, status, failed = 0;

	subtest = 1;
	for (i = 0; i < WAITERS; i++)
		if (pipe(pipes[i]) != 0) e(1);

	for (i = 0; i < WAITERS; i++) {
		if ((pids[i] = fork()) < 0) e(2);
		if (pids[i] == 0) {
			int fd = pipes[i][0];

			/* Half wait in select, half in poll. */
			if (i % 2 == 0) {
				fd_set rd;

				FD_ZERO(&rd);
				FD_SET(fd, &rd);
				if (select(fd + 1, &rd, NULL, NULL, NULL) != 1)
					_exit(10 + errno);
				_exit(FD_ISSET(fd, &rd) ? 0 : 2);
			} else {
				struct pollfd pfd;

				pfd.fd = fd;
				pfd.events = POLLIN;
				if (poll(&pfd, 1, -1) != 1)
					_exit(10 + errno);
				_exit((pfd.revents & POLLIN) ? 0 : 3);
			}
		}
	}

	/* Let them all get to waiting, then wake them all. */
	sleep(2);
	for (i = 0; i < WAITERS; i++)
		if (write(pipes[i][1], "x", 1) != 1) e(3);

	for (i = 0; i < WAITERS; i++) {
		if (waitpid(pids[i], &status, 0) != pids[i]) e(4);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			if (failed++ == 0)
				printf("waiter %d: status 0x%x\n", i, status);
		}
	}
	if (failed) e(5);

	for (i = 0; i < WAITERS; i++) {
		close(pipes[i][0]);
		close(pipes[i][1]);
	}
}

static void
test_pollnval(void)
{
	struct pollfd pfd[5];
	int p[2], closed;

	subtest = 2;
	if (pipe(p) != 0) e(1);
	if (write(p[1], "x", 1) != 1) e(2);
	if ((closed = dup(p[0])) < 0) e(3);
	close(closed);

	pfd[0].fd = closed;	pfd[0].events = POLLIN;	/* not open */
	pfd[1].fd = p[0];	pfd[1].events = POLLIN;	/* readable */
	pfd[2].fd = -1;		pfd[2].events = POLLIN;	/* ignored */
	pfd[3].fd = 300;	pfd[3].events = POLLIN;	/* cannot be open */
	pfd[4].fd = p[1];	pfd[4].events = 0;	/* nothing asked */

	/* No waiting with POLLNVAL to report, even with no timeout. */
	if (poll(pfd, 5, -1) != 3) e(4);
	if (pfd[0].revents != POLLNVAL) e(5);
	if (pfd[1].revents != POLLIN) e(6);
	if (pfd[2].revents != 0) e(7);
	if (pfd[3].revents != POLLNVAL) e(8);
	if (pfd[4].revents != 0) e(9);

	/* All of them bad. */
	if (poll(pfd, 1, 1000) != 1 || pfd[0].revents != POLLNVAL) e(10);

	/* Nothing ready, no wait. */
	{
		int q[2];

		if (pipe(q) != 0) e(11);
		pfd[0].fd = q[0];	pfd[0].events = POLLIN;
		if (poll(pfd, 1, 0) != 0 || pfd[0].revents != 0) e(12);
		pfd[0].fd = q[1];	pfd[0].events = POLLOUT;
		if (poll(pfd, 1, 0) != 1 || pfd[0].revents != POLLOUT) e(13);
		close(q[0]);
		close(q[1]);
	}

	/* select() itself still fails on a bad descriptor. */
	{
		fd_set rd;
		struct timeval tv = { 0, 0 };

		FD_ZERO(&rd);
		FD_SET(closed, &rd);
		if (select(closed + 1, &rd, NULL, NULL, &tv) != -1 ||
		    errno != EBADF) e(14);
	}

	close(p[0]);
	close(p[1]);
}

int
main(int argc, char **argv)
{

	start(129);

	test_many_waiters();
	test_pollnval();

	quit();
	return 0;
}
