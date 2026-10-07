/* Test 134 - getcwd() while other threads use relative names.
 *
 * The working directory belongs to the process, so getcwd() must not change
 * it even for a moment: a thread using a relative name meanwhile would find
 * another file, or none.  getcwd() used to chdir("..") up to the root and
 * back down.  Also, descriptors passed over a UNIX socket, which VFS installs
 * for the socket driver while another thread of the receiver opens files.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define LOOPS		2000
#define USERS		2

static char expect[PATH_MAX];
static int stop;
static int cwd_errors, name_errors;

static void *
cwd_loop(void *arg)
{
	char buf[PATH_MAX];
	int i;

	for (i = 0; i < LOOPS; i++) {
		if (getcwd(buf, sizeof(buf)) == NULL ||
		    strcmp(buf, expect) != 0)
			__atomic_fetch_add(&cwd_errors, 1, __ATOMIC_SEQ_CST);
	}
	__atomic_store_n(&stop, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

static void *
name_loop(void *arg)
{
	struct stat st;
	char name[16];
	int fd;

	snprintf(name, sizeof(name), "new%d", (int)(long)arg);
	while (!__atomic_load_n(&stop, __ATOMIC_SEQ_CST)) {
		if (stat("here", &st) != 0 ||
		    (fd = open("here", O_RDONLY)) < 0) {
			__atomic_fetch_add(&name_errors, 1, __ATOMIC_SEQ_CST);
			continue;
		}
		close(fd);
		if ((fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0) {
			__atomic_fetch_add(&name_errors, 1, __ATOMIC_SEQ_CST);
			continue;
		}
		close(fd);
		if (unlink(name) != 0)
			__atomic_fetch_add(&name_errors, 1, __ATOMIC_SEQ_CST);
	}
	return NULL;
}

static void
test_concurrent(void)
{
	pthread_t c, u[USERS];
	int fd, i;

	subtest = 1;
	if (mkdir("a", 0755) != 0 || mkdir("a/b", 0755) != 0) e(1);
	if (chdir("a/b") != 0) e(2);
	if ((fd = open("here", O_RDWR | O_CREAT, 0644)) < 0) e(3);
	close(fd);
	if (getcwd(expect, sizeof(expect)) == NULL) e(4);
	if (strlen(expect) < 4 || strcmp(expect + strlen(expect) - 4,
	    "/a/b") != 0) {
		printf("getcwd: %s\n", expect);
		e(5);
	}

	for (i = 0; i < USERS; i++)
		if (pthread_create(&u[i], NULL, name_loop, (void *)(long)i))
			e(6);
	if (pthread_create(&c, NULL, cwd_loop, NULL) != 0) e(7);
	if (pthread_join(c, NULL) != 0) e(8);
	for (i = 0; i < USERS; i++)
		if (pthread_join(u[i], NULL) != 0) e(9);
	if (cwd_errors != 0) {
		printf("%d bad getcwd() results\n", cwd_errors);
		e(10);
	}
	if (name_errors != 0) {
		printf("%d failed relative names\n", name_errors);
		e(11);
	}
	if (chdir("../..") != 0) e(12);
}

static void
test_names(void)
{
	char buf[PATH_MAX], home[PATH_MAX], small[3], *m;

	subtest = 2;
	if (getcwd(home, sizeof(home)) == NULL) e(10);
	if (chdir("/") != 0) e(1);
	if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/") != 0) e(2);
	if (chdir("/usr/bin") != 0) e(3);
	if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/usr/bin") != 0)
		e(4);
	if (getcwd(small, sizeof(small)) != NULL || errno != ERANGE) e(5);
	/* A failed getcwd() does not move the working directory. */
	if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/usr/bin") != 0)
		e(6);
	if ((m = getcwd(NULL, 0)) == NULL || strcmp(m, "/usr/bin") != 0) e(7);
	free(m);
	if (chdir(home) != 0) e(8);
}

#define PASSES		2000

static int sock;
static ino_t passed_ino, opened_ino;
static int pass_errors, open_errors;

static void *
receiver(void *arg)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	struct stat st;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} control;
	char c;
	int i, fd;

	for (i = 0; i < PASSES; i++) {
		memset(&msg, 0, sizeof(msg));
		iov.iov_base = &c;
		iov.iov_len = 1;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = control.buf;
		msg.msg_controllen = sizeof(control.buf);
		if (recvmsg(sock, &msg, 0) != 1 ||
		    (cmsg = CMSG_FIRSTHDR(&msg)) == NULL ||
		    cmsg->cmsg_type != SCM_RIGHTS) {
			pass_errors++;
			break;
		}
		memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
		/* One descriptor in flight at a time: UDS limits them. */
		if (write(sock, &c, 1) != 1)
			pass_errors++;
		/* The descriptor must be the file passed, not one that an
		 * open() of the other thread put in the same slot.
		 */
		if (fstat(fd, &st) != 0 || st.st_ino != passed_ino)
			pass_errors++;
		close(fd);
	}
	__atomic_store_n(&stop, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

static void *
opener(void *arg)
{
	struct stat st;
	int fd;

	while (!__atomic_load_n(&stop, __ATOMIC_SEQ_CST)) {
		if ((fd = open("opened", O_RDONLY)) < 0) {
			open_errors++;
			continue;
		}
		if (fstat(fd, &st) != 0 || st.st_ino != opened_ino)
			open_errors++;
		close(fd);
	}
	return NULL;
}

static void
sender(int s, int fd)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} control;
	char c = 'x';
	int i;

	for (i = 0; i < PASSES; i++) {
		memset(&msg, 0, sizeof(msg));
		iov.iov_base = &c;
		iov.iov_len = 1;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = control.buf;
		msg.msg_controllen = sizeof(control.buf);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
		if (sendmsg(s, &msg, 0) != 1) {
			printf("sendmsg %d: %s\n", i, strerror(errno));
			_exit(1);
		}
		if (read(s, &c, 1) != 1)	/* wait for the receiver */
			_exit(1);
	}
	_exit(0);
}

static void
test_passing(void)
{
	pthread_t r, o;
	struct stat st;
	pid_t pid;
	int sv[2], fd, status;

	subtest = 3;
	/* Descriptors received over a UNIX socket are installed by VFS for
	 * the socket driver (copyfd), outside the calls of the receiving
	 * process; meanwhile another thread of it opens files.  A descriptor
	 * that open() chose but has yet to install must not be given out.
	 */
	if ((fd = open("passed", O_RDWR | O_CREAT, 0644)) < 0) e(1);
	if (fstat(fd, &st) != 0) e(2);
	passed_ino = st.st_ino;
	close(fd);
	if ((fd = open("opened", O_RDWR | O_CREAT, 0644)) < 0) e(3);
	if (fstat(fd, &st) != 0) e(4);
	opened_ino = st.st_ino;
	close(fd);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) e(5);
	if ((pid = fork()) == 0) {
		close(sv[0]);
		if ((fd = open("passed", O_RDONLY)) < 0) _exit(2);
		sender(sv[1], fd);
	}
	if (pid < 0) e(6);
	close(sv[1]);
	sock = sv[0];

	stop = 0;
	pass_errors = open_errors = 0;
	if (pthread_create(&o, NULL, opener, NULL) != 0) e(7);
	if (pthread_create(&r, NULL, receiver, NULL) != 0) e(8);
	if (pthread_join(r, NULL) != 0) e(9);
	if (pthread_join(o, NULL) != 0) e(10);
	if (pass_errors != 0) {
		printf("%d bad passed descriptors\n", pass_errors);
		e(11);
	}
	if (open_errors != 0) {
		printf("%d bad opened descriptors\n", open_errors);
		e(12);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(13);
	close(sock);
}

int
main(int argc, char **argv)
{

	start(134);

	test_concurrent();
	test_names();
	test_passing();

	quit();
	return 0;
}
