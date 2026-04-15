/*	$NetBSD: proc.h (amd64 port)	*/

/*
 * Machine-dependent part of the lwp/proc structures for amd64.
 */

#ifndef _AMD64_PROC_H_
#define _AMD64_PROC_H_

#include <machine/frame.h>
#include <machine/pte.h>

struct pmap;
struct vm_page;
struct trapframe;

/*
 * Machine-dependent part of the lwp structure for amd64.
 */
struct mdlwp {
	struct	trapframe *md_regs;	/* registers on current frame */
	int	md_flags;		/* machine-dependent flags */
	volatile int md_astpending;	/* AST pending for this process */
	struct pmap *md_gc_pmap;	/* pmap being garbage collected */
	struct vm_page *md_gc_ptp;	/* pages from pmap g/c */
};

/* md_flags */
#define	MDL_IOPL	0x0002

struct mdproc {
	int	md_flags;
	void	(*md_syscall)(struct trapframe *);
};

/* md_flags */
#define MDP_USEDMTRR	0x0002

#endif /* _AMD64_PROC_H_ */
