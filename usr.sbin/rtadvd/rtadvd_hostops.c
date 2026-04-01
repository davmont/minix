/*	$NetBSD: rtadvd_hostops.c,v 1.1 2021/04/01 10:00:00 david Exp $	*/

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>

#include "prog_ops.h"

const struct prog_ops prog_ops = {
	.op_socket = socket,
	.op_close = close,
	.op_write = write,
	.op_sysctl = sysctl,
	.op_ioctl = ioctl,
	.op_clock_gettime = clock_gettime,
};
