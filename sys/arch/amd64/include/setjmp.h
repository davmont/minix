/*	$NetBSD: setjmp.h,v 1.4 2014/05/22 15:01:56 uebayasi Exp $ (adapted for amd64)	*/

/*
 * machine/setjmp.h: machine dependent setjmp-related information for x86-64.
 */

/*
 * jmp_buf layout for x86-64 (LP64):
 *   _JB_RBX     0   saved rbx
 *   _JB_RBP     1   saved rbp
 *   _JB_R12     2   saved r12
 *   _JB_R13     3   saved r13
 *   _JB_R14     4   saved r14
 *   _JB_R15     5   saved r15
 *   _JB_RSP     6   saved rsp
 *   _JB_PC      7   saved return address (rip)
 *   _JB_SIGFLAG 8   savemask flag (__sigsetjmp savemask argument)
 *   _JB_SIGMASK 9   signal mask (sigset_t = 2 longs on x86-64: slots 9,10)
 *
 * Total: 11 longs
 */
#define	_JBLEN		11	/* size, in longs (8 bytes each), of a jmp_buf */

#define	_JB_RBX		0
#define	_JB_RBP		1
#define	_JB_R12		2
#define	_JB_R13		3
#define	_JB_R14		4
#define	_JB_R15		5
#define	_JB_RSP		6
#define	_JB_PC		7
#define	_JB_SIGFLAG	8
#define	_JB_SIGMASK	9
