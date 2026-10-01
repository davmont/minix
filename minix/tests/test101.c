/* Test 101 - SA_RESTART.
 *
 * A system call blocked when a signal arrives fails with EINTR once the
 * handler has run -- unless the handler was installed with SA_RESTART, in
 * which case a restartable call (read/write on a pipe or socket, wait) is
 * made again and completes normally.  select() and sigsuspend() are never
 * restarted.
 */
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>

#include "common.h"

int max_error = 0;

static volatile int caught;

static void
handler(int sig)
{
	caught++;
}

static void
install(int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handler;
	sa.sa_flags = flags;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) e(90);
}

/* Fork a helper that sends us SIGUSR1 after 'sig_ms' and, if fd >= 0, then
 * writes one byte to fd after another 'write_ms'. */
static pid_t
helper(int sig_ms, int fd, int write_ms)
{
	pid_t me = getpid(), pid;

	if ((pid = fork()) < 0) e(91);
	if (pid == 0) {
		usleep(sig_ms * 1000);
		kill(me, SIGUSR1);
		if (fd >= 0) {
			usleep(write_ms * 1000);
			(void) write(fd, "x", 1);
		}
		_exit(0);
	}
	return pid;
}

static void
reap(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, 0) != pid) e(92);
}

/* read() from a pipe: restarted with SA_RESTART, EINTR without. */
static void
test_pipe(void)
{
	int fd[2];
	char c;
	pid_t pid;
	ssize_t r;

	subtest = 1;
	if (pipe(fd) != 0) e(1);

	install(SA_RESTART);
	caught = 0;
	pid = helper(100, fd[1], 100);
	r = read(fd[0], &c, 1);
	if (r != 1 || c != 'x') e(2);	/* restarted, then got the byte */
	if (caught != 1) e(3);
	reap(pid);

	install(0);
	caught = 0;
	pid = helper(100, fd[1], 300);
	r = read(fd[0], &c, 1);
	if (r != -1 || errno != EINTR) e(4);
	if (caught != 1) e(5);
	reap(pid);
	if (read(fd[0], &c, 1) != 1) e(6);	/* the helper's byte */

	close(fd[0]);
	close(fd[1]);
}

/* recv() on a socket: the same, through the socket cancel path. */
static void
test_socket(void)
{
	int sv[2];
	char c;
	pid_t pid;
	ssize_t r;

	subtest = 2;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) e(1);

	install(SA_RESTART);
	caught = 0;
	pid = helper(100, sv[1], 100);
	r = recv(sv[0], &c, 1, 0);
	if (r != 1 || c != 'x') e(2);
	if (caught != 1) e(3);
	reap(pid);

	install(0);
	caught = 0;
	pid = helper(100, sv[1], 300);
	r = recv(sv[0], &c, 1, 0);
	if (r != -1 || errno != EINTR) e(4);
	reap(pid);

	close(sv[0]);
	close(sv[1]);
}

/* waitpid(): restarted with SA_RESTART, EINTR without. */
static void
test_wait(void)
{
	pid_t child, pid;
	int status;

	subtest = 3;
	install(SA_RESTART);
	caught = 0;
	if ((child = fork()) < 0) e(1);
	if (child == 0) {
		usleep(300000);
		_exit(7);
	}
	pid = helper(100, -1, 0);
	if (waitpid(child, &status, 0) != child) e(2);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 7) e(3);
	if (caught != 1) e(4);
	reap(pid);

	install(0);
	caught = 0;
	if ((child = fork()) < 0) e(5);
	if (child == 0) {
		usleep(300000);
		_exit(7);
	}
	pid = helper(100, -1, 0);
	if (waitpid(child, &status, 0) != -1 || errno != EINTR) e(6);
	reap(pid);
	reap(child);
}

/* select() and sigsuspend() are never restarted. */
static void
test_never(void)
{
	fd_set rfds;
	struct timeval tv;
	sigset_t mask, old;
	int fd[2];
	pid_t pid;

	subtest = 4;
	install(SA_RESTART);
	if (pipe(fd) != 0) e(1);
	FD_ZERO(&rfds);
	FD_SET(fd[0], &rfds);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	caught = 0;
	pid = helper(100, -1, 0);
	if (select(fd[0] + 1, &rfds, NULL, NULL, &tv) != -1 || errno != EINTR)
		e(2);
	if (caught != 1) e(3);
	reap(pid);
	close(fd[0]);
	close(fd[1]);

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &mask, &old) != 0) e(4);
	caught = 0;
	pid = helper(100, -1, 0);
	if (sigsuspend(&old) != -1 || errno != EINTR) e(5);
	if (caught != 1) e(6);
	if (sigprocmask(SIG_SETMASK, &old, NULL) != 0) e(7);
	reap(pid);
}

int
main(int argc, char **argv)
{
	start(101);

	test_pipe();
	test_socket();
	test_wait();
	test_never();

	quit();
	return 0;
}
