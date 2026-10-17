/* Test 141 - closefrom(), ppoll(), getpassfd() and AT_SUN_EXECNAME.
 *
 * What OpenSSH needed of libc and exec, each of which was missing or wrong:
 *  1. closefrom() with nothing open from fd up succeeds and leaves errno
 *     alone (it returned -1, EBADF), and closes what is open.
 *  2. ppoll(): a timeout with nothing ready, a ready pipe, and a signal kept
 *     blocked by the mask while it waits.
 *  3. getpassfd() and getpass_r() exist and read more than 32 characters
 *     (the old getpass() kept 32).
 *  4. A static executable (this test is linked statically) finds its path in
 *     the aux vector, AT_SUN_EXECNAME, as getexecpath() does (it was empty).
 */
#include <sys/types.h>
#include <sys/exec_elf.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static volatile sig_atomic_t got;

static void
handler(int sig)
{
	got = sig;
}

static void
test_closefrom(void)
{
	int fd;

	subtest = 1;
	errno = 1234;
	if (closefrom(200) != 0) e(1);
	if (errno != 1234) e(2);
	if ((fd = open("/dev/null", O_RDONLY)) < 0) e(3);
	if (closefrom(fd) != 0) e(4);
	if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) e(5);
}

static void
test_ppoll(void)
{
	struct pollfd pfd;
	struct timespec ts = { 0, 100 * 1000 * 1000 };
	sigset_t mask, old;
	int p[2];

	subtest = 2;
	if (pipe(p) < 0) e(1);
	pfd.fd = p[0];
	pfd.events = POLLIN;
	if (ppoll(&pfd, 1, &ts, NULL) != 0) e(2);
	if (write(p[1], "x", 1) != 1) e(3);
	if (ppoll(&pfd, 1, &ts, NULL) != 1 || !(pfd.revents & POLLIN)) e(4);

	/* SIGUSR1 blocked during the wait: it stays pending. */
	signal(SIGUSR1, handler);
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, &old);
	kill(getpid(), SIGUSR1);
	got = 0;
	(void)read(p[0], &mask, 1);
	if (ppoll(&pfd, 1, &ts, &mask) != 0) e(5);
	if (got) e(6);
	sigprocmask(SIG_SETMASK, &old, NULL);
	if (got != SIGUSR1) e(7);
	close(p[0]);
	close(p[1]);
}

static void
test_getpass(void)
{
	static const char pw[] = "a passphrase longer than thirty-two characters";
	char buf[128];
	int p[2], fds[3], nul;

	subtest = 3;
	if (pipe(p) < 0 || (nul = open("/dev/null", O_WRONLY)) < 0) {
		e(1);
		return;
	}
	if (write(p[1], pw, strlen(pw)) != (ssize_t)strlen(pw) ||
	    write(p[1], "\n", 1) != 1) e(2);
	fds[0] = p[0]; fds[1] = nul; fds[2] = nul;
	if (getpassfd("", buf, sizeof(buf), fds, 0, 0) == NULL) e(3);
	else if (strcmp(buf, pw) != 0) e(4);
	close(p[0]); close(p[1]); close(nul);
}

static void
test_execname(const char *argv0)
{
	const AuxInfo *aux;
	const char *name = NULL;

	subtest = 4;
	for (aux = _dlauxinfo(); aux->a_type != AT_NULL; ++aux)
		if (aux->a_type == AT_SUN_EXECNAME)
			name = (const char *)(intptr_t)aux->a_v;
	if (name == NULL) e(1);
	else if (strcmp(name, argv0) != 0) e(2);
}

int
main(int argc, char **argv)
{

	start(141);
	test_closefrom();
	test_ppoll();
	test_getpass();
	test_execname(argv[0]);
	quit();
	return 0;
}
