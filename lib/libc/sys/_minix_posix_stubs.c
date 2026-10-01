/*
 * MINIX libc stubs for POSIX functions whose underlying syscalls or
 * subsystems are not implemented on MINIX.  Each stub returns the most
 * benign POSIX error so callers can at least link.  As MINIX gains the
 * real implementations, individual stubs should move out of this file.
 *
 * Conventions:
 *  - posix_fadvise: returns 0; the syscall is purely advisory.
 *  - extattr_*: ENOTSUP (no extended attribute support).
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

/* chflags/fchflags/lchflags now have real implementations in libc/sys. */

/* extattr_* now have real implementations in libc/sys/extattr.c. */

/* The *at family now has real implementations in minix/lib/libc/sys/m_at.c. */
