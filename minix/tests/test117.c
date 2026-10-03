/* Test 117 - O_SYNC and O_DSYNC writes.
 *
 * The suite part checks that synchronous writes work and keep their flags.
 * That the data is on the device when write() returns cannot be seen from
 * inside: the file servers share cached blocks through VM, so even the raw
 * device shows unwritten data.  "test117 w <file> sync|dsync|plain <tag>"
 * writes a pattern for a power-cut check from outside (kill the machine at
 * once, then look for the pattern in the disk image).
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define FILE1	"syncfile"
#define PATLEN	64

static void
make_pattern(char *pat, const char *tag)
{
	int i;

	snprintf(pat, PATLEN, "test117-%s-", tag);
	for (i = strlen(pat); i < PATLEN; i++)
		pat[i] = 'A' + (i * 7) % 26;
}

static void
power_cut_write(const char *path, const char *how, const char *tag)
{
	char pat[PATLEN];
	int fd, flags;

	flags = !strcmp(how, "sync") ? O_SYNC :
	    !strcmp(how, "dsync") ? O_DSYNC : 0;
	make_pattern(pat, tag);
	if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | flags, 0644)) < 0 ||
	    write(fd, pat, PATLEN) != PATLEN) {
		perror(path);
		exit(1);
	}
	/* No close() or fsync(): the machine goes down right after this. */
	printf("written %s\n", how);
	fflush(stdout);
	exit(0);
}

static void
check(int flag)
{
	char pat[PATLEN], buf[PATLEN];
	int fd;

	make_pattern(pat, "suite");
	unlink(FILE1);
	if ((fd = open(FILE1, O_RDWR | O_CREAT | O_TRUNC | flag, 0644)) < 0)
		e(1);
	if ((fcntl(fd, F_GETFL) & flag) != flag) e(2);
	if (write(fd, pat, PATLEN) != PATLEN) e(3);
	if (pwrite(fd, pat, PATLEN, 4096) != PATLEN) e(4);
	if (pread(fd, buf, PATLEN, 0) != PATLEN || memcmp(buf, pat, PATLEN))
		e(5);
	if (pread(fd, buf, PATLEN, 4096) != PATLEN || memcmp(buf, pat, PATLEN))
		e(6);
	if (write(fd, pat, 0) != 0) e(7);		/* nothing to flush */
	close(fd);
	unlink(FILE1);
}

int
main(int argc, char **argv)
{

	if (argc == 5 && !strcmp(argv[1], "w"))
		power_cut_write(argv[2], argv[3], argv[4]);

	start(117);

	subtest = 1;
	check(O_SYNC);
	subtest = 2;
	check(O_DSYNC);
	subtest = 3;
	check(O_SYNC | O_DSYNC);

	subtest = 4;
	/* The flags can be set and cleared later, as on NetBSD. */
	{
		int fd, fl;

		if ((fd = open(FILE1, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
			e(1);
		if ((fl = fcntl(fd, F_GETFL)) == -1 || (fl & O_SYNC)) e(2);
		if (fcntl(fd, F_SETFL, fl | O_SYNC) != 0) e(3);
		if (!(fcntl(fd, F_GETFL) & O_SYNC)) e(4);
		if (write(fd, "x", 1) != 1) e(5);
		if (fcntl(fd, F_SETFL, fl) != 0) e(6);
		if (fcntl(fd, F_GETFL) & O_SYNC) e(7);
		close(fd);
		unlink(FILE1);
	}

	quit();
	return 0;
}
