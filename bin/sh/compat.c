#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

int
dup3(int oldfd, int newfd, int flags)
{
	if (oldfd == newfd) {
		errno = EINVAL;
		return -1;
	}
	if (dup2(oldfd, newfd) == -1)
		return -1;
	if (flags & O_CLOEXEC) {
		fcntl(newfd, F_SETFD, FD_CLOEXEC);
	}
	return newfd;
}

int
fstatat(int fd, const char *path, struct stat *st, int flags)
{
	if (fd == AT_FDCWD || path[0] == '/') {
		return stat(path, st);
	}

	int cwd = open(".", O_RDONLY);
	if (cwd == -1)
		return -1;
	if (fchdir(fd) == -1) {
		int serrno = errno;
		close(cwd);
		errno = serrno;
		return -1;
	}
	int ret = stat(path, st);
	int serrno = errno;
	fchdir(cwd);
	close(cwd);
	errno = serrno;
	return ret;
}
