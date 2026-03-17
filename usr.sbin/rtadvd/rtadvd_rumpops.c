/*	$NetBSD: rtadvd_rumpops.c,v 1.1 2021/04/01 10:00:00 david Exp $	*/

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include <rump/rumpclient.h>

#include "prog_ops.h"

const struct prog_ops prog_ops = {
	.op_init =	rumpclient_init,

	.op_socket =	rump_sys_socket,
	.op_close =	rump_sys_close,
	.op_write =	rump_sys_write,
	.op_sysctl =	rump_sys___sysctl,
	.op_ioctl =	rump_sys_ioctl,
	.op_clock_gettime = rump_sys_clock_gettime,
};
