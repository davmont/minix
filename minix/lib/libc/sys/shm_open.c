/*	POSIX shared memory - shm_open(2), shm_unlink(3)			*/
/*
 * A shared memory object is a file under /var/shm, where the system mounts
 * a tmpfs (as NetBSD does): its mmap(2) maps the file's pages themselves,
 * MAP_SHARED mappings write to them, and read(2), write(2), ftruncate(2) and
 * fstat(2) work on it as on any file.
 */

#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __weak_alias
__weak_alias(shm_open, _shm_open)
__weak_alias(shm_unlink, _shm_unlink)
#endif

#define SHM_DIR		"/var/shm/"

/*
 * Map a POSIX shm name to the path of its file.  POSIX allows only a leading
 * slash and no other slashes; be lenient about leading slashes, strict about
 * embedded ones.
 */
static int
shm_path(const char *name, char *buf, size_t bufsz)
{
	/* POSIX: ENAMETOOLONG for a name longer than PATH_MAX, or with a
	 * component longer than NAME_MAX, before anything else. */
	const char *c;
	size_t n;

	if (strlen(name) >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}
	for (c = name; *c != '\0'; c += n) {
		c += strspn(c, "/");
		if ((n = strcspn(c, "/")) > NAME_MAX) {
			errno = ENAMETOOLONG;
			return -1;
		}
	}
	while (*name == '/')
		name++;
	if (*name == '\0' || strchr(name, '/') != NULL ||
	    strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		errno = EINVAL;
		return -1;
	}
	if ((size_t)snprintf(buf, bufsz, "%s%s", SHM_DIR, name) >= bufsz) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

int
shm_open(const char *name, int oflag, mode_t mode)
{
	char path[PATH_MAX];

	if (shm_path(name, path, sizeof(path)) < 0)
		return -1;

	/* POSIX: the descriptor has FD_CLOEXEC set. */
	return open(path, oflag | O_CLOEXEC | O_NOFOLLOW, mode);
}

int
shm_unlink(const char *name)
{
	char path[PATH_MAX];

	if (shm_path(name, path, sizeof(path)) < 0)
		return -1;
	if (unlink(path) == 0)
		return 0;
	/* /var/shm is sticky: unlink(2) says EPERM for another user's object,
	 * where POSIX wants EACCES from shm_unlink(), as glibc gives. */
	if (errno == EPERM)
		errno = EACCES;
	return -1;
}
