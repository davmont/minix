/*	getcwd() - get the name of the current working directory.
 *							Author: Kees J. Bot
 *								30 Apr 1989
 */

#include <sys/cdefs.h>
#include "namespace.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>

/* libc-private interface */
int __getcwd(char *, size_t);

static int addpath(const char *path, char **ap, const char *entry)
/* Add the name of a directory entry at the front of the path being built.
 * Note that the result always starts with a slash.
 */
{
	const char *e= entry;
	char *p= *ap;

	while (*e != 0) e++;

	while (e > entry && p > path) *--p = *--e;

	if (p == path) return -1;
	*--p = '/';
	*ap= p;
	return 0;
}

int __getcwd(char *path, size_t size)
/* Build the name of the working directory from the bottom up: find the name
 * of each directory in its parent.  The walk goes through directory
 * descriptors (openat, fstatat): it used to chdir("..") all the way up and
 * then back down, which moves the working directory of every thread of the
 * process, so that their relative names meanwhile led elsewhere.
 */
{
	struct stat above, current, tmp;
	struct dirent *entry;
	DIR *d;
	char *p;
	int fd, ufd, dfd, cycle, e;

	if (path == NULL || size == 0) { errno= EINVAL; return -1; }
	if (size == 1) { errno= ERANGE; return -1; }	/* even "/" needs 2 */

	p= path + size;
	*--p = 0;

	/* "." itself is not opened: it may be searchable but not readable,
	 * and only the directories above are read.
	 */
	fd= AT_FDCWD;
	if (stat(".", &current) < 0) return -1;

	while (1) {
		ufd= openat(fd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (ufd < 0) goto fail;
		if (fstat(ufd, &above) < 0) goto fail_up;

		if (above.st_dev == current.st_dev
					&& above.st_ino == current.st_ino) {
			close(ufd);
			break;	/* Root dir found */
		}

		/* The directory stream gets a descriptor of its own, as
		 * closedir() closes it; ufd stays for fstatat().
		 */
		if ((dfd= fcntl(ufd, F_DUPFD_CLOEXEC, 0)) < 0) goto fail_up;
		if ((d= fdopendir(dfd)) == NULL) {
			close(dfd);
			goto fail_up;
		}

		/* Cycle is 0 for a simple inode nr search, or 1 for a search
		 * for inode *and* device nr.
		 */
		cycle= above.st_dev == current.st_dev ? 0 : 1;

		do {
			tmp.st_ino= 0;
			if ((entry= readdir(d)) == NULL) {
				switch (++cycle) {
				case 1:
					rewinddir(d);
					continue;
				case 2:
					closedir(d);
					errno= ENOENT;
					goto fail_up;
				}
			}
			if (strcmp(entry->d_name, ".") == 0) continue;
			if (strcmp(entry->d_name, "..") == 0) continue;

			switch (cycle) {
			case 0:
				/* Simple test on inode nr. */
				if (entry->d_ino != current.st_ino) continue;
				/*FALL THROUGH*/

			case 1:
				/* Current is mounted. */
				if (fstatat(ufd, entry->d_name, &tmp,
				    AT_SYMLINK_NOFOLLOW) < 0) continue;
				break;
			}
		} while (tmp.st_ino != current.st_ino
					|| tmp.st_dev != current.st_dev);

		if (addpath(path, &p, entry->d_name) < 0) {
			closedir(d);
			errno = ERANGE;
			goto fail_up;
		}
		closedir(d);
		if (fd != AT_FDCWD) close(fd);
		fd= ufd;
		current= above;
	}
	if (fd != AT_FDCWD) close(fd);

	if (*p == 0) *--p = '/';	/* Cwd is "/" if nothing added */
	if (p > path)			/* Move string to start of path. */
		memmove(path, p, strlen(p) + 1);
	return 0;

fail_up:
	e= errno;
	close(ufd);
	errno= e;
fail:
	e= errno;
	if (fd != AT_FDCWD) close(fd);
	errno= e;
	return -1;
}
