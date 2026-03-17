/*      $NetBSD: prog_ops.h,v 1.1 2021/04/01 10:00:00 david Exp $	*/

#ifndef _PROG_OPS_H_
#define _PROG_OPS_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

#ifndef CRUNCHOPS
struct prog_ops {
	int (*op_init)(void);

	int (*op_socket)(int, int, int);
	int (*op_ioctl)(int, unsigned long, ...);
	int (*op_close)(int);

	ssize_t (*op_write)(int, const void *, size_t);

	int (*op_sysctl)(const int *, u_int, void *, size_t *,
			 const void *, size_t);

	int (*op_clock_gettime)(clockid_t, struct timespec *);
};
extern const struct prog_ops prog_ops;

#define prog_init prog_ops.op_init
#define prog_socket prog_ops.op_socket
#define prog_ioctl prog_ops.op_ioctl
#define prog_close prog_ops.op_close
#define prog_write prog_ops.op_write
#define prog_sysctl prog_ops.op_sysctl
#define prog_clock_gettime prog_ops.op_clock_gettime
#else
#define prog_init ((int (*)(void))NULL)
#define prog_socket socket
#define prog_ioctl ioctl
#define prog_close close
#define prog_write write
#define prog_sysctl sysctl
#define prog_clock_gettime clock_gettime
#endif

#endif /* _PROG_OPS_H_ */
