/*	wlprobe - probe the shared-memory primitives Wayland needs
 *
 * Wayland's wl_shm buffer path is the reason POSIX shm exists on MINIX:
 * a client shm_open()s a pool, ftruncate()s it, mmap()s it writable and
 * MAP_SHARED, draws into it, then hands the *file descriptor* to the
 * compositor over a SCM_RIGHTS control message.  The compositor mmap()s
 * that same descriptor and must observe the client's pixels -- i.e. the
 * mapping has to be genuinely shared, not copy-on-write.
 *
 * Historically MINIX had no fd-backed writable shared memory: file mmaps
 * COW'd (see mem_file.c / mappedfile_writable()), so a shared writable
 * MAP_SHARED on a file was rejected outright.  This probe checks each
 * link of that chain independently so a regression tells you *which*
 * link broke, not merely that Wayland stopped working.
 *
 * Exits 0 only if every check passes.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define POOL_SIZE	4096
#define SHM_NAME	"/wlprobe-pool"

/* Sentinels: the parent stamps MAGIC_P, the child answers with MAGIC_C. */
#define MAGIC_P		0x5741594cUL	/* "WAYL" */
#define MAGIC_C		0x414e4421UL	/* "AND!" */

static int failures;

static void
check(const char *what, int ok, const char *detail)
{
	if (ok) {
		printf("%-46s -> OK\n", what);
	} else {
		printf("%-46s -> FAIL (%s)\n", what, detail);
		failures++;
	}
	fflush(stdout);
}

/*
 * Send fd across a unix socket the way a Wayland client hands its pool to
 * the compositor: one dummy data byte plus a SCM_RIGHTS control message.
 * (A control message with no data payload is not portable.)
 */
static int
send_fd(int sock, int fd)
{
	struct msghdr msg;
	struct iovec iov;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr align;	/* force cmsghdr alignment */
		char buf[CMSG_SPACE(sizeof(int))];
	} u;
	char dummy = 'x';

	memset(&msg, 0, sizeof(msg));
	memset(&u, 0, sizeof(u));

	iov.iov_base = &dummy;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = u.buf;
	msg.msg_controllen = sizeof(u.buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

	return sendmsg(sock, &msg, 0) == 1 ? 0 : -1;
}

static int
recv_fd(int sock)
{
	struct msghdr msg;
	struct iovec iov;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr align;
		char buf[CMSG_SPACE(sizeof(int))];
	} u;
	char dummy;
	int fd;

	memset(&msg, 0, sizeof(msg));
	memset(&u, 0, sizeof(u));

	iov.iov_base = &dummy;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = u.buf;
	msg.msg_controllen = sizeof(u.buf);

	if (recvmsg(sock, &msg, 0) != 1)
		return -1;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET ||
	    cmsg->cmsg_type != SCM_RIGHTS ||
	    cmsg->cmsg_len != CMSG_LEN(sizeof(int)))
		return -1;

	memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
	return fd;
}

int
main(void)
{
	volatile unsigned long *pool;
	struct stat st;
	int fd, sv[2], rfd, status;
	pid_t pid;

	printf("wlprobe: POSIX shm / wl_shm prerequisites\n\n");

	/* Any stale pool from a previous crashed run would poison the test. */
	(void)shm_unlink(SHM_NAME);

	/* 1. shm_open() must create an anonymous, fd-addressable object. */
	fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, 0600);
	check("shm_open(O_CREAT|O_EXCL)", fd >= 0, strerror(errno));
	if (fd < 0)
		return 1;

	/* 2. ftruncate() sizes the pool; wl_shm_create_pool() relies on it. */
	check("ftruncate(pool, 4096)", ftruncate(fd, POOL_SIZE) == 0,
	    strerror(errno));

	if (fstat(fd, &st) == 0)
		check("fstat reports the truncated size",
		    st.st_size == POOL_SIZE, "size mismatch");
	else
		check("fstat reports the truncated size", 0, strerror(errno));

	/*
	 * 3. The load-bearing one.  A writable MAP_SHARED mapping of an fd is
	 * exactly what MINIX used to refuse with ENXIO.
	 */
	pool = mmap(NULL, POOL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	check("wl_shm: mmap(MAP_SHARED, PROT_WRITE)", pool != MAP_FAILED,
	    strerror(errno));
	if (pool == MAP_FAILED) {
		(void)shm_unlink(SHM_NAME);
		return 1;
	}

	pool[0] = MAGIC_P;
	check("write through the mapping", pool[0] == MAGIC_P, "readback");

	/*
	 * 4. Hand the fd to a separate process over SCM_RIGHTS and have it map
	 * the pool.  This is the full compositor handshake: if the mapping were
	 * COW, the child's store would be private and the parent would still
	 * see MAGIC_P.
	 */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
		err(1, "socketpair");

	if ((pid = fork()) < 0)
		err(1, "fork");

	if (pid == 0) {
		volatile unsigned long *cpool;
		int cfd;

		close(sv[0]);
		if ((cfd = recv_fd(sv[1])) < 0)
			_exit(10);

		cpool = mmap(NULL, POOL_SIZE, PROT_READ | PROT_WRITE,
		    MAP_SHARED, cfd, 0);
		if (cpool == MAP_FAILED)
			_exit(11);

		/* Did the parent's write reach us? */
		if (cpool[0] != MAGIC_P)
			_exit(12);

		/* Answer, so the parent can prove the sharing is bidirectional. */
		cpool[1] = MAGIC_C;
		_exit(0);
	}

	close(sv[1]);
	check("SCM_RIGHTS: pass shm fd to peer", send_fd(sv[0], fd) == 0,
	    strerror(errno));

	if (waitpid(pid, &status, 0) != pid)
		err(1, "waitpid");

	check("peer receives the fd",
	    WIFEXITED(status) && WEXITSTATUS(status) != 10, "recvmsg/SCM_RIGHTS");
	check("peer mmap()s the received fd",
	    WIFEXITED(status) && WEXITSTATUS(status) != 11, "peer mmap failed");
	check("peer sees the parent's write (shared, not COW)",
	    WIFEXITED(status) && WEXITSTATUS(status) != 12, "peer saw stale data");
	check("parent sees the peer's write (bidirectional)",
	    pool[1] == MAGIC_C, "write did not propagate back");
	check("peer exited cleanly",
	    WIFEXITED(status) && WEXITSTATUS(status) == 0, "see codes above");

	/* 5. Unlink removes the name; the mapping must stay valid until munmap. */
	check("shm_unlink", shm_unlink(SHM_NAME) == 0, strerror(errno));
	check("mapping survives unlink", pool[0] == MAGIC_P, "mapping died");
	check("shm_unlink of a gone name fails with ENOENT",
	    shm_unlink(SHM_NAME) == -1 && errno == ENOENT, strerror(errno));

	(void)munmap((void *)pool, POOL_SIZE);
	(void)close(fd);

	printf("\nwlprobe: %s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
	return failures == 0 ? 0 : 1;
}
