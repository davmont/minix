/* IPC vector constants for x86-64 Minix.
 *
 * On x86-64, Minix uses the SYSCALL instruction rather than software
 * interrupts.  The call-type selector (IPCVEC_UM / KERVEC_UM) is passed
 * in %rdi so the kernel entry stub can dispatch to the right handler.
 *
 * The numeric values are deliberately kept identical to the i386 values so
 * that any shared code (e.g. kernel_call.c) does not need separate constants.
 */
#ifndef _IPCCONST_H
#define _IPCCONST_H

#define KERVEC_INTR	32	/* legacy INT-based kernel call (unused on amd64) */
#define IPCVEC_INTR	33	/* legacy INT-based IPC (unused on amd64) */

#define KERVEC_UM	34	/* SYSCALL-based kernel call */
#define IPCVEC_UM	35	/* SYSCALL-based IPC */

/* Register used by the kernel to pass IPC status back to userspace. */
#define IPC_STATUS_REG	rbx

#endif /* _IPCCONST_H */
