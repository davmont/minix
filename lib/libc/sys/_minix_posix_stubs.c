/*
 * MINIX libc stubs for POSIX functions whose underlying syscalls or
 * subsystems are not implemented on MINIX.  Each stub returns the most
 * benign POSIX error so callers can at least link.  As MINIX gains the
 * real implementations, individual stubs should move out of this file.
 *
 * Conventions:
 *  - posix_fadvise: returns 0; the syscall is purely advisory.
 *  - chflags/fchflags: ENOTSUP (per-file flags not implemented).
 *  - extattr_*: ENOTSUP (no extended attribute support).
 *  - openat/fstatat/linkat/unlinkat/readlinkat/fchmodat/fchownat:
 *    fall through to the non-at variant when dirfd == AT_FDCWD or
 *    the path is absolute; otherwise ENOSYS.  This is enough for
 *    well-behaved userland that defaults to AT_FDCWD; code that
 *    actually depends on per-directory fds will fail loudly.
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/extattr.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

/* -------- posix_fadvise: advisory no-op -------------------------- */

int
posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
	(void)fd; (void)offset; (void)len; (void)advice;
	return 0;
}

/* -------- chflags / fchflags: no per-file flags ------------------ */

int
chflags(const char *path, unsigned long flags)
{
	(void)path; (void)flags;
	errno = ENOTSUP;
	return -1;
}

int
fchflags(int fd, unsigned long flags)
{
	(void)fd; (void)flags;
	errno = ENOTSUP;
	return -1;
}

/* -------- extattr_*: no extended attribute support --------------- */

ssize_t
extattr_get_fd(int fd, int attrnamespace, const char *attrname,
    void *data, size_t nbytes)
{
	(void)fd; (void)attrnamespace; (void)attrname;
	(void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

ssize_t
extattr_get_file(const char *path, int attrnamespace, const char *attrname,
    void *data, size_t nbytes)
{
	(void)path; (void)attrnamespace; (void)attrname;
	(void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

ssize_t
extattr_get_link(const char *path, int attrnamespace, const char *attrname,
    void *data, size_t nbytes)
{
	(void)path; (void)attrnamespace; (void)attrname;
	(void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

ssize_t
extattr_list_fd(int fd, int attrnamespace, void *data, size_t nbytes)
{
	(void)fd; (void)attrnamespace; (void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

ssize_t
extattr_list_file(const char *path, int attrnamespace,
    void *data, size_t nbytes)
{
	(void)path; (void)attrnamespace; (void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

ssize_t
extattr_list_link(const char *path, int attrnamespace,
    void *data, size_t nbytes)
{
	(void)path; (void)attrnamespace; (void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

int
extattr_set_fd(int fd, int attrnamespace, const char *attrname,
    const void *data, size_t nbytes)
{
	(void)fd; (void)attrnamespace; (void)attrname;
	(void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

int
extattr_set_file(const char *path, int attrnamespace, const char *attrname,
    const void *data, size_t nbytes)
{
	(void)path; (void)attrnamespace; (void)attrname;
	(void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

int
extattr_set_link(const char *path, int attrnamespace, const char *attrname,
    const void *data, size_t nbytes)
{
	(void)path; (void)attrnamespace; (void)attrname;
	(void)data; (void)nbytes;
	errno = ENOTSUP;
	return -1;
}

int
extattr_delete_fd(int fd, int attrnamespace, const char *attrname)
{
	(void)fd; (void)attrnamespace; (void)attrname;
	errno = ENOTSUP;
	return -1;
}

int
extattr_delete_file(const char *path, int attrnamespace, const char *attrname)
{
	(void)path; (void)attrnamespace; (void)attrname;
	errno = ENOTSUP;
	return -1;
}

int
extattr_delete_link(const char *path, int attrnamespace, const char *attrname)
{
	(void)path; (void)attrnamespace; (void)attrname;
	errno = ENOTSUP;
	return -1;
}

/* -------- *at family: AT_FDCWD / absolute-path fallback ---------- */

/*
 * Treat the dirfd as usable when it's AT_FDCWD or the path is
 * absolute (in which case any dirfd is ignored by POSIX).  Anything
 * else needs real per-directory resolution that MINIX doesn't have.
 */
static int
at_use_cwd(int dirfd, const char *path)
{
	return dirfd == AT_FDCWD || (path != NULL && path[0] == '/');
}

int
openat(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		/* mode_t promotes to int through varargs. */
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
	}
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	return open(path, flags, mode);
}

int
fstatat(int dirfd, const char *path, struct stat *sb, int flag)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	if (flag & AT_SYMLINK_NOFOLLOW)
		return lstat(path, sb);
	return stat(path, sb);
}

int
linkat(int fd1, const char *name1, int fd2, const char *name2, int flag)
{
	(void)flag;
	if (!at_use_cwd(fd1, name1) || !at_use_cwd(fd2, name2)) {
		errno = ENOSYS;
		return -1;
	}
	return link(name1, name2);
}

int
unlinkat(int dirfd, const char *path, int flag)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	if (flag & AT_REMOVEDIR)
		return rmdir(path);
	return unlink(path);
}

ssize_t
readlinkat(int dirfd, const char *path, char *buf, size_t bufsize)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	return readlink(path, buf, bufsize);
}

int
fchmodat(int dirfd, const char *path, mode_t mode, int flag)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	if (flag & AT_SYMLINK_NOFOLLOW)
		return lchmod(path, mode);
	return chmod(path, mode);
}

int
fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flag)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	if (flag & AT_SYMLINK_NOFOLLOW)
		return lchown(path, uid, gid);
	return chown(path, uid, gid);
}

int
faccessat(int dirfd, const char *path, int mode, int flag)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	if (flag & AT_EACCESS) {
		errno = ENOSYS;
		return -1;
	}
	return access(path, mode);
}

int
renameat(int fromfd, const char *from, int tofd, const char *to)
{
	if (!at_use_cwd(fromfd, from) || !at_use_cwd(tofd, to)) {
		errno = ENOSYS;
		return -1;
	}
	return rename(from, to);
}

int
symlinkat(const char *target, int dirfd, const char *path)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	return symlink(target, path);
}

int
mkdirat(int dirfd, const char *path, mode_t mode)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	return mkdir(path, mode);
}

int
mkfifoat(int dirfd, const char *path, mode_t mode)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	return mkfifo(path, mode);
}

int
mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
	if (!at_use_cwd(dirfd, path)) {
		errno = ENOSYS;
		return -1;
	}
	return mknod(path, mode, dev);
}
