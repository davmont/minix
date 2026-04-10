#include "kernel/kernel.h"
#include "include/arch_proto.h"

/*
 * On amd64 we support two IPC styles: the software-interrupt fallback and
 * the SYSCALL fast path.  SYSENTER is an Intel-only 32-bit instruction and
 * is not available in 64-bit long mode, so minix_ipcvecs_sysenter is omitted.
 */

struct minix_ipcvecs minix_ipcvecs_softint __section(".usermapped") = {
	.send		= usermapped_send_softint,
	.receive	= usermapped_receive_softint,
	.sendrec	= usermapped_sendrec_softint,
	.sendnb		= usermapped_sendnb_softint,
	.notify		= usermapped_notify_softint,
	.do_kernel_call	= usermapped_do_kernel_call_softint,
	.senda		= usermapped_senda_softint
};

struct minix_ipcvecs minix_ipcvecs_syscall __section(".usermapped") = {
	.send		= usermapped_send_syscall,
	.receive	= usermapped_receive_syscall,
	.sendrec	= usermapped_sendrec_syscall,
	.sendnb		= usermapped_sendnb_syscall,
	.notify		= usermapped_notify_syscall,
	.do_kernel_call	= usermapped_do_kernel_call_syscall,
	.senda		= usermapped_senda_syscall
};
