/* Test 105 - FIFO rendezvous.
 *
 * Two processes pass data back and forth through a named pipe, closing and
 * reopening it each way, as test31 does once; here many rounds in a row.
 * Each open of a FIFO blocks until the other side opens it too, so every
 * round exercises the open rendezvous and the last-close of the pipe.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 1;

#define ROUNDS	300
#define FIFO	"fifo"

/* Report a failed step with its real result: errno is cleared before each
 * call, so a stale value cannot pass for the cause. */
static void
fail(const char *who, int round, const char *step, ssize_t r, int err)
{
	printf("%s round %d: %s returned %zd, errno %d (%s)\n", who, round,
	    step, r, err, err ? strerror(err) : "-");
	fflush(stdout);
}

/* One direction: open for writing, write 'msg', close. */
static int
send_msg(const char *who, int round, const char *msg)
{
	ssize_t r;
	int fd;

	errno = 0;
	if ((fd = open(FIFO, O_WRONLY)) < 0) {
		fail(who, round, "open(O_WRONLY)", fd, errno);
		return -1;
	}
	errno = 0;
	if ((r = write(fd, msg, strlen(msg) + 1)) != (ssize_t)strlen(msg) + 1) {
		fail(who, round, "write", r, errno);
		close(fd);
		return -1;
	}
	errno = 0;
	if ((r = close(fd)) != 0) {
		fail(who, round, "close(w)", r, errno);
		return -1;
	}
	return 0;
}

/* The other direction: open for reading, read 'msg', close. */
static int
recv_msg(const char *who, int round, const char *msg)
{
	char buf[64];
	ssize_t r;
	int fd;

	errno = 0;
	if ((fd = open(FIFO, O_RDONLY)) < 0) {
		fail(who, round, "open(O_RDONLY)", fd, errno);
		return -1;
	}
	memset(buf, 0, sizeof(buf));
	errno = 0;
	if ((r = read(fd, buf, sizeof(buf))) != (ssize_t)strlen(msg) + 1 ||
	    strcmp(buf, msg) != 0) {
		fail(who, round, "read", r, errno);
		close(fd);
		return -1;
	}
	errno = 0;
	if ((r = close(fd)) != 0) {
		fail(who, round, "close(r)", r, errno);
		return -1;
	}
	return 0;
}

static void
test_pingpong(void)
{
	int round, status;
	pid_t pid;

	subtest = 1;
	unlink(FIFO);
	if (mkfifo(FIFO, 0644) != 0) e(1);

	/* A write to a FIFO without a reader must show up as EPIPE here, not
	 * kill the writer. */
	signal(SIGPIPE, SIG_IGN);

	if ((pid = fork()) < 0) e(2);
	if (pid == 0) {
		for (round = 0; round < ROUNDS; round++) {
			if (recv_msg("child", round, "banana") != 0 ||
			    send_msg("child", round, "thanks") != 0)
				_exit(1);
		}
		_exit(0);
	}

	alarm(120);		/* in case the rendezvous hangs */
	for (round = 0; round < ROUNDS; round++) {
		if (send_msg("parent", round, "banana") != 0 ||
		    recv_msg("parent", round, "thanks") != 0) {
			e(3);
			break;
		}
	}
	if (round < ROUNDS)
		kill(pid, SIGKILL);
	if (waitpid(pid, &status, 0) != pid) e(4);
	else if (round == ROUNDS && (!WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)) e(5);
	alarm(0);

	unlink(FIFO);
}

int
main(int argc, char **argv)
{
	start(105);

	test_pingpong();

	quit();
	return 0;
}
