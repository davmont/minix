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

	/*
	 * 5. Grow the pool, as wl_shm_pool_resize() does on every window resize:
	 * ftruncate() bigger, then mmap() the same fd at the new size.  This used
	 * to hand back a mapping still of the OLD size -- the IPC server honoured
	 * the length only on the first mmap() -- so the caller faulted the moment
	 * it wrote past the old end.  Check the far end of the grown pool, since
	 * that is the part that did not exist before.
	 */
	{
		volatile unsigned long *big;

		check("ftruncate(pool, 8192): grow",
		    ftruncate(fd, POOL_SIZE * 2) == 0, strerror(errno));

		big = mmap(NULL, POOL_SIZE * 2, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, 0);
		check("wl_shm_pool_resize: mmap grown pool",
		    big != MAP_FAILED, strerror(errno));

		if (big != MAP_FAILED) {
			/* Contents written before the grow must survive it. */
			check("grown pool keeps the old contents",
			    big[0] == MAGIC_P, "data lost across the grow");

			/* The bytes past the old end must be real memory. */
			big[(POOL_SIZE * 2) / sizeof(*big) - 1] = MAGIC_C;
			check("grown pool is writable past the old end",
			    big[(POOL_SIZE * 2) / sizeof(*big) - 1] == MAGIC_C,
			    "readback failed beyond the old size");

			(void)munmap((void *)big, POOL_SIZE * 2);
		}
	}

	/*
	 * 6. The idiom every real Wayland client uses (os_create_anonymous_file):
	 * open the object, unlink the name AT ONCE, and keep only the fd -- then
	 * size it and map it.  POSIX guarantees the object outlives its name for
	 * as long as a descriptor is held, and a client that could not do this
	 * would fail at mmap().  Worth its own check precisely because every step
	 * above passed while this did not work.
	 */
	{
		int afd;
		volatile unsigned long *ap;

		afd = shm_open("/wlprobe-anon", O_RDWR | O_CREAT | O_EXCL, 0600);
		check("anonymous pool: shm_open", afd >= 0, strerror(errno));

		if (afd >= 0) {
			check("anonymous pool: unlink the name immediately",
			    shm_unlink("/wlprobe-anon") == 0, strerror(errno));

			check("anonymous pool: ftruncate after unlink",
			    ftruncate(afd, POOL_SIZE) == 0, strerror(errno));

			ap = mmap(NULL, POOL_SIZE, PROT_READ | PROT_WRITE,
			    MAP_SHARED, afd, 0);
			check("anonymous pool: mmap the unlinked fd",
			    ap != MAP_FAILED, strerror(errno));

			if (ap != MAP_FAILED) {
				ap[0] = MAGIC_C;
				check("anonymous pool: writable",
				    ap[0] == MAGIC_C, "readback failed");
				(void)munmap((void *)ap, POOL_SIZE);
			}
			(void)close(afd);
		}
	}

	/* 7. Unlink removes the name; the mapping must stay valid until munmap. */
	check("shm_unlink", shm_unlink(SHM_NAME) == 0, strerror(errno));
	check("mapping survives unlink", pool[0] == MAGIC_P, "mapping died");
	check("shm_unlink of a gone name fails with ENOENT",
	    shm_unlink(SHM_NAME) == -1 && errno == ENOENT, strerror(errno));

	(void)munmap((void *)pool, POOL_SIZE);
	(void)close(fd);

	printf("\nwlprobe: %s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
	return failures == 0 ? 0 : 1;
}
