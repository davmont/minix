/* Test 109 - pread(2) and pwrite(2): at an offset, without using or changing
 * the file position, and atomically, also between threads sharing the
 * descriptor.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define ROUNDS	20000

static int fd;

static void
test_semantics(void)
{
	char buf[32];
	int p[2];

	subtest = 1;
	if ((fd = open("pfile", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(1);
	if (write(fd, "0123456789", 10) != 10) e(2);
	if (lseek(fd, 4, SEEK_SET) != 4) e(3);

	memset(buf, 0, sizeof(buf));
	if (pread(fd, buf, 3, 7) != 3 || memcmp(buf, "789", 3)) e(4);
	if (lseek(fd, 0, SEEK_CUR) != 4) e(5);		/* unchanged */
	if (pread(fd, buf, 5, 8) != 2 || memcmp(buf, "89", 2)) e(6);
	if (pread(fd, buf, 5, 10) != 0) e(7);		/* at EOF */

	if (pwrite(fd, "AB", 2, 1) != 2) e(8);
	if (lseek(fd, 0, SEEK_CUR) != 4) e(9);
	/* Past the end: the file grows, the gap reads as zeroes. */
	if (pwrite(fd, "Z", 1, 15) != 1) e(10);
	if (lseek(fd, 0, SEEK_END) != 16) e(11);
	if (lseek(fd, 4, SEEK_SET) != 4) e(12);
	if (pread(fd, buf, 16, 0) != 16) e(13);
	if (memcmp(buf, "0AB3456789\0\0\0\0\0Z", 16)) e(14);

	/* The plain read continues from the unchanged position. */
	if (read(fd, buf, 2) != 2 || memcmp(buf, "45", 2)) e(15);

	if (pread(fd, buf, 1, -1) != -1 || errno != EINVAL) e(16);
	if (pwrite(fd, "x", 1, -5) != -1 || errno != EINVAL) e(17);
	if (pread(-1, buf, 1, 0) != -1 || errno != EBADF) e(18);

	/* Pipes have no position. */
	if (pipe(p) != 0) e(19);
	if (write(p[1], "x", 1) != 1) e(20);
	if (pread(p[0], buf, 1, 0) != -1 || errno != ESPIPE) e(21);
	if (pwrite(p[1], "x", 1, 0) != -1 || errno != ESPIPE) e(22);
	close(p[0]);
	close(p[1]);

	/* Restore "0123456789" for the threads. */
	if (pwrite(fd, "0123456789", 10, 0) != 10) e(23);
}

static volatile int stop;
static int bad[2];

static void *
reader(void *arg)
{
	int i, n = (int)(long)arg;
	off_t off = (n == 0) ? 0 : 9;
	char want = (n == 0) ? '0' : '9', c;

	for (i = 0; i < ROUNDS && !stop; i++) {
		if (pread(fd, &c, 1, off) != 1 || c != want) {
			bad[n]++;
			stop = 1;
		}
	}
	return NULL;
}

static void
test_threads(void)
{
	pthread_t t[2];
	int i;

	subtest = 2;
	/* Two threads on one descriptor, at different offsets: each must get
	 * its own byte every time.  (With pread as lseek, read, lseek, they
	 * move the shared file position under each other.) */
	stop = 0;
	bad[0] = bad[1] = 0;
	for (i = 0; i < 2; i++)
		if (pthread_create(&t[i], NULL, reader, (void *)(long)i) != 0)
			e(1);
	for (i = 0; i < 2; i++)
		if (pthread_join(t[i], NULL) != 0) e(2);
	if (bad[0] != 0 || bad[1] != 0) e(3);
	close(fd);
	unlink("pfile");
}

int
main(int argc, char **argv)
{
	start(109);

	test_semantics();
	test_threads();

	quit();
	return 0;
}
