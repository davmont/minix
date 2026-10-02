/* Test 110 - accept4(2) and paccept(2): flags of the accepted socket. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define SOCKPATH	"sock110"

static int lfd;

/* Queue a connection on the listening socket: a non-blocking client socket
 * in this process connects (it completes or is in progress), so that the
 * accept below finds it.  Returns the client socket. */
static int
connect_client(void)
{
	struct sockaddr_un sun;
	int s;

	if ((s = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0) {
		e(80);
		return -1;
	}
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, SOCKPATH, sizeof(sun.sun_path));
	if (connect(s, (struct sockaddr *)&sun, sizeof(sun)) != 0 &&
	    errno != EINPROGRESS) {
		printf("connect: errno %d (%s)\n", errno, strerror(errno));
		fflush(stdout);
		e(81);
	}
	return s;
}

static void
finish(int afd, int cfd)
{
	close(afd);
	close(cfd);
}

static void
check(int afd, int cloexec, int nonblock)
{
	int fl;

	if ((fl = fcntl(afd, F_GETFD)) == -1) e(90);
	if (!!(fl & FD_CLOEXEC) != cloexec) e(91);
	if ((fl = fcntl(afd, F_GETFL)) == -1) e(92);
	if (!!(fl & O_NONBLOCK) != nonblock) e(93);
}

static void
test_accept4(void)
{
	struct sockaddr_un sun;
	int afd, cfd;

	subtest = 1;
	unlink(SOCKPATH);
	if ((lfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) e(1);
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, SOCKPATH, sizeof(sun.sun_path));
	if (bind(lfd, (struct sockaddr *)&sun, sizeof(sun)) != 0) e(2);
	if (listen(lfd, 5) != 0) e(3);

	cfd = connect_client();
	if ((afd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC)) < 0) e(4);
	check(afd, 1, 0);
	finish(afd, cfd);

	cfd = connect_client();
	if ((afd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK)) < 0) e(5);
	check(afd, 0, 1);
	finish(afd, cfd);

	cfd = connect_client();
	if ((afd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK)) < 0)
		e(6);
	check(afd, 1, 1);
	finish(afd, cfd);

	/* No flags: as accept(2). */
	cfd = connect_client();
	if ((afd = accept4(lfd, NULL, NULL, 0)) < 0) e(7);
	check(afd, 0, 0);
	finish(afd, cfd);
	cfd = connect_client();
	if ((afd = accept(lfd, NULL, NULL)) < 0) e(8);
	check(afd, 0, 0);
	finish(afd, cfd);

	if (accept4(lfd, NULL, NULL, 0x1) != -1 || errno != EINVAL)
		e(9);
	if (accept4(-1, NULL, NULL, SOCK_CLOEXEC) != -1 || errno != EBADF)
		e(10);
}

static void
test_paccept(void)
{
	sigset_t mask, old, now;
	int afd, cfd;

	subtest = 2;
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	if (sigprocmask(SIG_SETMASK, NULL, &old) != 0) e(1);

	cfd = connect_client();
	if ((afd = paccept(lfd, NULL, NULL, &mask, SOCK_CLOEXEC)) < 0) e(2);
	check(afd, 1, 0);
	finish(afd, cfd);
	/* The caller's mask is back. */
	if (sigprocmask(SIG_SETMASK, NULL, &now) != 0) e(3);
	if (sigismember(&now, SIGUSR1) != sigismember(&old, SIGUSR1)) e(4);

	cfd = connect_client();
	if ((afd = paccept(lfd, NULL, NULL, NULL, SOCK_NONBLOCK)) < 0) e(5);
	check(afd, 0, 1);
	finish(afd, cfd);

	close(lfd);
	unlink(SOCKPATH);
}

int
main(int argc, char **argv)
{
	start(110);

	test_accept4();
	test_paccept();

	quit();
	return 0;
}
