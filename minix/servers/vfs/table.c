/* This file contains the table used to map system call numbers onto the
 * routines that perform them.
 */

#define _TABLE

#include "fs.h"
#include <minix/callnr.h>
#include <minix/com.h>
#include "file.h"
#include "lock.h"
#include "vnode.h"
#include "vmnt.h"

#define CALL(n) [((n) - VFS_BASE)]

int (* const call_vec[NR_VFS_CALLS])(void) = {
	CALL(VFS_READ)		= do_read,		/* read(2) */
	CALL(VFS_WRITE)		= do_write,		/* write(2) */
	CALL(VFS_LSEEK)		= do_lseek,		/* lseek(2) */
	CALL(VFS_CLOSE)		= do_close,		/* close(2) */
	CALL(VFS_CHDIR)		= do_chdir,		/* chdir(2) */
	CALL(VFS_MOUNT)		= do_mount,		/* mount(2) */
	CALL(VFS_UMOUNT)	= do_umount,		/* umount(2) */
	CALL(VFS_SYNC)		= do_sync,		/* sync(2) */
	CALL(VFS_FSTAT)		= do_fstat,		/* fstat(2) */
	CALL(VFS_IOCTL)		= do_ioctl,		/* ioctl(2) */
	CALL(VFS_FCNTL)		= do_fcntl,		/* fcntl(2) */
	CALL(VFS_PIPE2)		= do_pipe2,		/* pipe2(2) */
	CALL(VFS_UMASK)		= do_umask,		/* umask(2) */
	CALL(VFS_CHROOT)	= do_chroot,		/* chroot(2) */
	CALL(VFS_GETDENTS)	= do_getdents,		/* getdents(2) */
	CALL(VFS_SELECT)	= do_select,		/* select(2) */
	CALL(VFS_FCHDIR)	= do_fchdir,		/* fchdir(2) */
	CALL(VFS_FSYNC)		= do_fsync,		/* fsync(2) */
	CALL(VFS_TRUNCATE)	= do_truncate,		/* truncate(2) */
	CALL(VFS_FTRUNCATE)	= do_ftruncate,		/* ftruncate(2) */
	CALL(VFS_FCHMOD)	= do_chmod,		/* fchmod(2) */
	CALL(VFS_FCHOWN)	= do_chown,		/* fchown(2) */
	CALL(VFS_VMCALL)	= do_vm_call,
	CALL(VFS_GETVFSSTAT)	= do_getvfsstat,	/* getvfsstat(2) */
	CALL(VFS_STATVFS1)	= do_statvfs,		/* statvfs(2) */
	CALL(VFS_FSTATVFS1)	= do_fstatvfs,		/* fstatvfs(2) */
	CALL(VFS_GETRUSAGE)	= do_getrusage,		/* (obsolete) */
	CALL(VFS_SVRCTL)	= do_svrctl,		/* svrctl(2) */
	CALL(VFS_GCOV_FLUSH)	= do_gcov_flush,	/* gcov_flush(2) */
	CALL(VFS_MAPDRIVER)	= do_mapdriver,		/* mapdriver(2) */
	CALL(VFS_COPYFD)	= do_copyfd,		/* copyfd(2) */
	CALL(VFS_SOCKETPATH)	= do_socketpath,	/* socketpath(2) */
	CALL(VFS_GETSYSINFO)	= do_getsysinfo,	/* getsysinfo(2) */
	CALL(VFS_SOCKET)	= do_socket,		/* socket(2) */
	CALL(VFS_SOCKETPAIR)	= do_socketpair,	/* socketpair(2) */
	CALL(VFS_BIND)		= do_bind,		/* bind(2) */
	CALL(VFS_CONNECT)	= do_connect,		/* connect(2) */
	CALL(VFS_LISTEN)	= do_listen,		/* listen(2) */
	CALL(VFS_ACCEPT)	= do_accept,		/* accept(2) */
	CALL(VFS_SENDTO)	= do_sendto,		/* sendto(2) */
	CALL(VFS_SENDMSG)	= do_sockmsg,		/* sendmsg(2) */
	CALL(VFS_RECVFROM)	= do_recvfrom,		/* recvfrom(2) */
	CALL(VFS_RECVMSG)	= do_sockmsg,		/* recvmsg(2) */
	CALL(VFS_SETSOCKOPT)	= do_setsockopt,	/* setsockopt(2) */
	CALL(VFS_GETSOCKOPT)	= do_getsockopt,	/* getsockopt(2) */
	CALL(VFS_GETSOCKNAME)	= do_getsockname,	/* getsockname(2) */
	CALL(VFS_GETPEERNAME)	= do_getpeername,	/* getpeername(2) */
	CALL(VFS_SHUTDOWN)	= do_shutdown,		/* shutdown(2) */
	CALL(VFS_CHFLAGS)	= do_chflags,		/* chflags(2) */
	CALL(VFS_FCHFLAGS)	= do_chflags,		/* fchflags(2) */
	CALL(VFS_EXTATTR_GET)	= do_extattr,		/* extattr_get_file/link(2) */
	CALL(VFS_EXTATTR_SET)	= do_extattr,		/* extattr_set_file/link(2) */
	CALL(VFS_EXTATTR_LIST)	= do_extattr,		/* extattr_list_file/link(2) */
	CALL(VFS_EXTATTR_DELETE) = do_extattr,		/* extattr_delete_file/link(2) */
	CALL(VFS_EXTATTR_GET_FD) = do_extattr,		/* extattr_get_fd(2) */
	CALL(VFS_EXTATTR_SET_FD) = do_extattr,		/* extattr_set_fd(2) */
	CALL(VFS_EXTATTR_LIST_FD) = do_extattr,		/* extattr_list_fd(2) */
	CALL(VFS_EXTATTR_DELETE_FD) = do_extattr,	/* extattr_delete_fd(2) */
	CALL(VFS_SWAPCTL) = do_swapctl,			/* swapctl (phase C) */
	CALL(VFS_OPENAT)	= do_openat,		/* openat(2), open(2) */
	CALL(VFS_FSTATAT)	= do_fstatat,		/* fstatat(2), [l]stat(2) */
	CALL(VFS_MKDIRAT)	= do_mkdirat,		/* mkdirat(2), mkdir(2) */
	CALL(VFS_MKNODAT)	= do_mknodat,		/* mknodat(2), mknod(2) */
	CALL(VFS_UNLINKAT)	= do_unlinkat,		/* unlinkat(2), unlink, rmdir */
	CALL(VFS_LINKAT)	= do_linkat,		/* linkat(2), link(2) */
	CALL(VFS_RENAMEAT)	= do_renameat,		/* renameat(2), rename(2) */
	CALL(VFS_SYMLINKAT)	= do_symlinkat,		/* symlinkat(2), symlink(2) */
	CALL(VFS_READLINKAT)	= do_readlinkat,	/* readlinkat(2), readlink */
	CALL(VFS_FCHMODAT)	= do_chmod,		/* fchmodat(2), [l]chmod(2) */
	CALL(VFS_FCHOWNAT)	= do_chown,		/* fchownat(2), [l]chown(2) */
	CALL(VFS_FACCESSAT)	= do_access,		/* faccessat(2), access(2) */
	CALL(VFS_UTIMENSAT)	= do_utimensat,		/* utimensat(2), futimens(2) */
	CALL(VFS_PREAD)		= do_pread,		/* pread(2) */
	CALL(VFS_PWRITE)	= do_pwrite,		/* pwrite(2) */
	CALL(VFS_FLOCK)		= do_flock,		/* flock(2) */
	CALL(VFS_FALLOCATE)	= do_fallocate,		/* posix_fallocate(2) */
};
