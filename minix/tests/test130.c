/* Test 130 - kqueue(2) and kevent(2). */
#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static const struct timespec zero = { 0, 0 };

/* kevent() with one change (or none, if 'filter' is -1) and room for 'n'
 * events.
 */
static int
kev(int kq, uintptr_t ident, int filter, int flags, int fflags, int64_t data,
	struct kevent *out, int n, const struct timespec *ts)
{
	struct kevent ch;

	if (filter == -1)
		return kevent(kq, NULL, 0, out, n, ts);
	EV_SET(&ch, ident, filter, flags, fflags, data, (intptr_t)0x1234);
	return kevent(kq, &ch, 1, out, n, ts);
}

static long
elapsed_ms(struct timespec *t0)
{
	struct timespec t1;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	return (t1.tv_sec - t0->tv_sec) * 1000 +
	    (t1.tv_nsec - t0->tv_nsec) / 1000000;
}

static void
test_basics(void)
{
	struct kevent ev;
	char c;
	int kq, p[2];

	subtest = 1;
	if ((kq = kqueue()) < 0) e(1);
	if (read(kq, &c, 1) != -1 || errno != EINVAL) e(2);
	if (pipe(p) != 0) e(3);
	if (kevent(p[0], NULL, 0, &ev, 1, &zero) != -1 || errno != EBADF) e(4);

	/* Nothing ready yet. */
	if (kev(kq, p[0], EVFILT_READ, EV_ADD, 0, 0, &ev, 1, &zero) != 0) e(5);
	/* Data: one event, with the byte count and the user data. */
	if (write(p[1], "abc", 3) != 3) e(6);
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, &zero) != 1) e(7);
	if (ev.ident != (uintptr_t)p[0] || ev.filter != EVFILT_READ) e(8);
	if (ev.data != 3 || ev.udata != (intptr_t)0x1234) e(9);
	if (ev.flags & EV_EOF) e(10);
	/* Still there (level-triggered). */
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, &zero) != 1) e(11);
	/* The writer goes: EOF. */
	close(p[1]);
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, &zero) != 1) e(12);
	if (!(ev.flags & EV_EOF)) e(13);

	/* Writing end of a pipe. */
	close(p[0]);
	if (pipe(p) != 0) e(14);
	if (kev(kq, p[1], EVFILT_WRITE, EV_ADD, 0, 0, &ev, 1, &zero) != 1)
		e(15);
	if (ev.filter != EVFILT_WRITE || ev.data <= 0) e(16);

	/* Closing a descriptor removes its knotes. */
	close(p[1]);
	if (kev(kq, p[1], EVFILT_WRITE, EV_DELETE, 0, 0, &ev, 1, &zero) != 1)
		e(17);
	if (!(ev.flags & EV_ERROR) || ev.data != EBADF) e(18);	/* as BSD */
	close(p[0]);
	close(kq);
}

static void
test_flags(void)
{
	struct kevent ev[4], ch[3];
	int kq, p[2];

	subtest = 2;
	if ((kq = kqueue1(O_CLOEXEC)) < 0) e(1);
	if (!(fcntl(kq, F_GETFD) & FD_CLOEXEC)) e(2);
	if (pipe(p) != 0) e(3);
	if (write(p[1], "x", 1) != 1) e(4);

	/* EV_ONESHOT: reported once, then gone. */
	if (kev(kq, p[0], EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, ev, 1,
	    &zero) != 1) e(5);
	if (kev(kq, 0, -1, 0, 0, 0, ev, 1, &zero) != 0) e(6);

	/* EV_DISABLE, EV_ENABLE, EV_DISPATCH. */
	if (kev(kq, p[0], EVFILT_READ, EV_ADD | EV_DISABLE, 0, 0, ev, 1,
	    &zero) != 0) e(7);
	if (kev(kq, p[0], EVFILT_READ, EV_ENABLE, 0, 0, ev, 1, &zero) != 1)
		e(8);
	if (kev(kq, p[0], EVFILT_READ, EV_ADD | EV_DISPATCH, 0, 0, ev, 1,
	    &zero) != 1) e(9);
	if (kev(kq, 0, -1, 0, 0, 0, ev, 1, &zero) != 0) e(10);
	if (kev(kq, p[0], EVFILT_READ, EV_DELETE, 0, 0, ev, 1, &zero) != 0)
		e(11);

	/* Errors: in the event list if there is room, else the call's. */
	if (kev(kq, 200, EVFILT_READ, EV_ADD, 0, 0, ev, 1, NULL) != 1) e(12);
	if (!(ev[0].flags & EV_ERROR) || ev[0].data != EBADF) e(13);
	if (kev(kq, 200, EVFILT_READ, EV_ADD, 0, 0, ev, 0, NULL) != -1 ||
	    errno != EBADF) e(14);
	if (kev(kq, 1, 99, EV_ADD, 0, 0, ev, 1, NULL) != 1 ||
	    ev[0].data != EINVAL) e(15);
	if (kev(kq, kq, EVFILT_READ, EV_ADD, 0, 0, ev, 1, NULL) != 1 ||
	    ev[0].data != EINVAL) e(16);	/* a kqueue on itself */

	/* EV_RECEIPT: a result for each change, data 0 for success. */
	EV_SET(&ch[0], p[0], EVFILT_READ, EV_ADD | EV_RECEIPT, 0, 0, 0);
	EV_SET(&ch[1], 201, EVFILT_READ, EV_ADD | EV_RECEIPT, 0, 0, 0);
	EV_SET(&ch[2], p[1], EVFILT_WRITE, EV_ADD | EV_RECEIPT, 0, 0, 0);
	if (kevent(kq, ch, 3, ev, 4, &zero) != 3) e(17);
	if (ev[0].data != 0 || ev[1].data != EBADF || ev[2].data != 0) e(18);
	/* Both ready now. */
	if (kev(kq, 0, -1, 0, 0, 0, ev, 4, &zero) != 2) e(19);
	/* With room for one only. */
	if (kev(kq, 0, -1, 0, 0, 0, ev, 1, &zero) != 1) e(20);

	close(p[0]);
	close(p[1]);
	close(kq);
}

static void
test_wait(void)
{
	struct timespec t0, ts;
	struct kevent ev;
	pid_t pid;
	int kq, p[2], s[2], status;

	subtest = 3;
	if ((kq = kqueue()) < 0) e(1);
	if (pipe(p) != 0) e(2);
	if (kev(kq, p[0], EVFILT_READ, EV_ADD, 0, 0, NULL, 0, NULL) != 0) e(3);

	/* Woken by a write from elsewhere. */
	if ((pid = fork()) < 0) e(4);
	if (pid == 0) {
		usleep(200000);
		(void)write(p[1], "y", 1);
		_exit(0);
	}
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, NULL) != 1) e(5);
	if (ev.ident != (uintptr_t)p[0]) e(6);
	if (elapsed_ms(&t0) < 100) e(7);
	if (waitpid(pid, &status, 0) != pid) e(8);

	/* A timeout. */
	{
		char c;
		if (read(p[0], &c, 1) != 1) e(9);
	}
	ts.tv_sec = 0;
	ts.tv_nsec = 300000000;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, &ts) != 0) e(10);
	if (elapsed_ms(&t0) < 250) e(11);

	/* Sockets, through their driver. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) != 0) e(12);
	if (kev(kq, s[0], EVFILT_READ, EV_ADD, 0, 0, &ev, 1, &zero) != 0)
		e(13);
	if (write(s[1], "z", 1) != 1) e(14);
	ts.tv_sec = 2;
	ts.tv_nsec = 0;
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, &ts) != 1) e(15);
	if (ev.ident != (uintptr_t)s[0]) e(16);

	/* A child cannot use the kqueue it inherits. */
	if ((pid = fork()) < 0) e(17);
	if (pid == 0)
		_exit(kevent(kq, NULL, 0, &ev, 1, &zero) == -1 &&
		    errno == EBADF ? 0 : 1);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(18);

	close(s[0]);
	close(s[1]);
	close(p[0]);
	close(p[1]);
	close(kq);
}

static void
test_timer(void)
{
	struct timespec t0;
	struct kevent ev;
	int kq;

	subtest = 4;
	if ((kq = kqueue()) < 0) e(1);

	/* Periodic, every 50 ms. */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (kev(kq, 7, EVFILT_TIMER, EV_ADD, 0, 50, &ev, 1, NULL) != 1) e(2);
	if (ev.ident != 7 || ev.filter != EVFILT_TIMER || ev.data < 1) e(3);
	if (elapsed_ms(&t0) < 40) {
		printf("first expiry after %ld ms\n", elapsed_ms(&t0));
		e(4);
	}
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, NULL) != 1) e(5);
	if (elapsed_ms(&t0) < 90) {
		printf("second expiry after %ld ms\n", elapsed_ms(&t0));
		e(6);
	}
	/* Missed periods are counted. */
	usleep(220000);
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, &zero) != 1 || ev.data < 3) e(7);
	if (kev(kq, 7, EVFILT_TIMER, EV_DELETE, 0, 0, &ev, 1, &zero) != 0)
		e(8);

	/* One-shot: once, then gone. */
	if (kev(kq, 8, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, 30, &ev, 1,
	    NULL) != 1) e(9);
	if (kev(kq, 8, EVFILT_TIMER, EV_DELETE, 0, 0, &ev, 1, &zero) != 1 ||
	    ev.data != ENOENT) e(10);

	close(kq);
}

static int ukq;

static void *
trigger(void *arg)
{
	struct kevent ch;

	usleep(200000);
	EV_SET(&ch, 9, EVFILT_USER, 0, NOTE_TRIGGER | NOTE_FFOR | 0x5, 0, 0);
	(void)kevent(ukq, &ch, 1, NULL, 0, NULL);
	return NULL;
}

static void
test_user(void)
{
	struct timespec t0;
	struct kevent ev;
	pthread_t t;

	subtest = 5;
	if ((ukq = kqueue()) < 0) e(1);
	if (kev(ukq, 9, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, &ev, 1,
	    &zero) != 0) e(2);
	if (kev(ukq, 9, EVFILT_USER, 0, NOTE_TRIGGER | NOTE_FFCOPY | 0x12, 0,
	    &ev, 1, &zero) != 1) e(3);
	if (ev.ident != 9 || ev.fflags != 0x12) e(4);
	/* EV_CLEAR: not again until triggered again. */
	if (kev(ukq, 0, -1, 0, 0, 0, &ev, 1, &zero) != 0) e(5);

	/* Triggered by another thread while waiting. */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (pthread_create(&t, NULL, trigger, NULL) != 0) e(6);
	if (kev(ukq, 0, -1, 0, 0, 0, &ev, 1, NULL) != 1) e(7);
	if (ev.ident != 9 || ev.fflags != 0x17) e(8);
	if (elapsed_ms(&t0) < 100) e(9);
	if (pthread_join(t, NULL) != 0) e(10);
	close(ukq);
}

static void
test_file(void)
{
	struct timespec ts;
	struct kevent ev[2];
	pid_t pid;
	char buf[16];
	int kq, fd, wfd, status;

	subtest = 6;
	if ((kq = kqueue()) < 0) e(1);
	if ((fd = open("t130", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(2);
	if (write(fd, "hello", 5) != 5) e(3);
	if (lseek(fd, 0, SEEK_SET) != 0) e(4);

	/* Readable while not at the end: data is what is left. */
	if (kev(kq, fd, EVFILT_READ, EV_ADD, 0, 0, ev, 1, &zero) != 1) e(5);
	if (ev[0].data != 5) e(6);
	if (read(fd, buf, 5) != 5) e(7);
	if (kev(kq, 0, -1, 0, 0, 0, ev, 1, &zero) != 0) e(8);

	/* Appended to by another process: wakes up the wait, as for tail -f;
	 * also a vnode note.
	 */
	if (kev(kq, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
	    NOTE_WRITE | NOTE_EXTEND, 0, ev, 1, &zero) != 0) e(9);
	if ((pid = fork()) < 0) e(10);
	if (pid == 0) {
		usleep(200000);
		if ((wfd = open("t130", O_WRONLY | O_APPEND)) < 0) _exit(1);
		_exit(write(wfd, "world", 5) == 5 ? 0 : 2);
	}
	ts.tv_sec = 5;
	ts.tv_nsec = 0;
	{
		int i, n, got_read = 0, got_vnode = 0, tries;

		/* Both events come, in one call or two. */
		for (tries = 0; tries < 3 && !(got_read && got_vnode);
		    tries++) {
			if ((n = kev(kq, 0, -1, 0, 0, 0, ev, 2, &ts)) < 1)
				e(11);
			for (i = 0; i < n; i++) {
				if (ev[i].filter == EVFILT_READ) {
					if (ev[i].data != 5) e(13);
					got_read = 1;
				} else if (ev[i].filter == EVFILT_VNODE) {
					if (ev[i].fflags !=
					    (NOTE_WRITE | NOTE_EXTEND)) e(14);
					got_vnode = 1;
				} else
					e(15);
			}
		}
		if (!got_read || !got_vnode) e(16);
	}
	if (waitpid(pid, &status, 0) != pid || status != 0) e(12);
	/* The vnode note is cleared once reported. */
	if (kev(kq, fd, EVFILT_READ, EV_DELETE, 0, 0, ev, 1, &zero) != 0)
		e(18);
	if (kev(kq, 0, -1, 0, 0, 0, ev, 1, &zero) != 0) e(19);

	close(fd);
	close(kq);
	(void)unlink("t130");
}

#define ALL_NOTES (NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | \
	NOTE_LINK | NOTE_RENAME | NOTE_REVOKE)

/* A kqueue watching 'fd' for all the vnode notes. */
static int
watch(int fd)
{
	int kq;

	if ((kq = kqueue()) < 0) e(90);
	if (kev(kq, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, ALL_NOTES, 0, NULL,
	    0, NULL) != 0) e(91);
	return kq;
}

/* The vnode notes pending on a watch() kqueue (EV_CLEAR: reading clears). */
static int
notes(int kq)
{
	struct kevent ev;
	int n;

	if ((n = kev(kq, 0, -1, 0, 0, 0, &ev, 1, &zero)) < 0) e(92);
	if (n == 0)
		return 0;
	if (ev.filter != EVFILT_VNODE) e(93);
	return ev.fflags;
}

static void
test_vnode_notes(void)
{
	struct timeval tv[2];
	int dkq, fkq, xkq, dfd, ffd, xfd, fd;

	subtest = 9;
	/* Watching a directory, and all the notes, as libuv's fs_event does
	 * (only writes to regular files used to be accepted).
	 */
	if (mkdir("d", 0755) != 0) e(1);
	if ((fd = open("d/f", O_RDWR | O_CREAT, 0644)) < 0) e(2);
	close(fd);
	if ((dfd = open("d", O_RDONLY)) < 0) e(3);
	if ((ffd = open("d/f", O_RDONLY)) < 0) e(4);
	dkq = watch(dfd);
	fkq = watch(ffd);
	if (notes(dkq) != 0 || notes(fkq) != 0) e(5);

	/* An entry added: a write to the directory, nothing for the file. */
	if ((fd = open("d/g", O_RDWR | O_CREAT, 0644)) < 0) e(6);
	close(fd);
	if (notes(dkq) != NOTE_WRITE) e(7);
	if (notes(fkq) != 0) e(8);

	/* Attributes. */
	if (chmod("d/f", 0600) != 0) e(9);
	if (notes(fkq) != NOTE_ATTRIB) e(10);
	memset(tv, 0, sizeof(tv));
	if (utimes("d/f", tv) != 0) e(11);
	if (notes(fkq) != NOTE_ATTRIB) e(12);
	if (notes(dkq) != 0) e(13);

	/* Links. */
	if (link("d/f", "d/h") != 0) e(14);
	if (notes(fkq) != NOTE_LINK) e(15);
	if (notes(dkq) != NOTE_WRITE) e(16);
	if (unlink("d/h") != 0) e(17);
	if (notes(fkq) != NOTE_LINK) e(18);	/* not its last link */
	if (notes(dkq) != NOTE_WRITE) e(19);

	/* Renamed. */
	if (rename("d/f", "d/f2") != 0) e(20);
	if (notes(fkq) != NOTE_RENAME) e(21);
	if (notes(dkq) != NOTE_WRITE) e(22);

	/* A subdirectory: also a link count change of the parent. */
	if (mkdir("d/sub", 0755) != 0) e(23);
	if (notes(dkq) != (NOTE_WRITE | NOTE_LINK)) e(24);
	if (rmdir("d/sub") != 0) e(25);
	if (notes(dkq) != (NOTE_WRITE | NOTE_LINK)) e(26);

	/* Replaced by a rename: that was its last link. */
	if (rename("d/g", "d/f2") != 0) e(27);
	if (notes(fkq) != NOTE_DELETE) e(28);
	if (notes(dkq) != NOTE_WRITE) e(29);

	/* Unlinked, its last link. */
	if ((fd = open("d/x", O_RDWR | O_CREAT, 0644)) < 0) e(30);
	close(fd);
	if ((xfd = open("d/x", O_RDONLY)) < 0) e(31);
	xkq = watch(xfd);
	(void)notes(dkq);
	if (unlink("d/x") != 0) e(32);
	if (notes(xkq) != NOTE_DELETE) e(33);
	if (notes(dkq) != NOTE_WRITE) e(34);

	close(xkq);
	close(fkq);
	close(dkq);
	close(xfd);
	close(ffd);
	close(dfd);
	(void)unlink("d/f2");
	(void)rmdir("d");
}

static int ckq;

static void *
closer(void *arg)
{

	usleep(200000);
	close(ckq);
	return NULL;
}

static void
test_close_while_waiting(void)
{
	struct kevent ev;
	pthread_t t;

	subtest = 8;
	/* Another thread closes the kqueue: the wait ends, with EBADF. */
	if ((ckq = kqueue()) < 0) e(1);
	if (pthread_create(&t, NULL, closer, NULL) != 0) e(2);
	if (kev(ckq, 0, -1, 0, 0, 0, &ev, 1, NULL) != -1 || errno != EBADF)
		e(3);
	if (pthread_join(t, NULL) != 0) e(4);
}

static void
on_alarm(int sig)
{
}

static void
test_eintr(void)
{
	struct sigaction sa;
	struct kevent ev;
	int kq, p[2];

	subtest = 7;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(1);
	if ((kq = kqueue()) < 0) e(2);
	if (pipe(p) != 0) e(3);
	if (kev(kq, p[0], EVFILT_READ, EV_ADD, 0, 0, NULL, 0, NULL) != 0) e(4);
	alarm(1);
	if (kev(kq, 0, -1, 0, 0, 0, &ev, 1, NULL) != -1 || errno != EINTR)
		e(5);
	close(p[0]);
	close(p[1]);
	close(kq);
}

int
main(int argc, char **argv)
{

	start(130);

	test_basics();
	test_flags();
	test_wait();
	test_timer();
	test_user();
	test_file();
	test_eintr();
	test_close_while_waiting();
	test_vnode_notes();

	quit();
	return 0;
}
