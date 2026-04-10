/*	$NetBSD: frame.h (amd64 port)	*/

#ifndef _AMD64_FRAME_H_
#define _AMD64_FRAME_H_

#if defined(_KERNEL) || defined(__minix)

/*
 * Signal frame for old-style signals (sigcontext-based) on amd64.
 * Placed on the user stack by the kernel when delivering a signal.
 */
struct sigframe_sigcontext {
#if defined(__minix)
	long	sf_ra_sigreturn;	/* return address to sigreturn */
#else
	long	sf_ra;			/* return address for handler */
#endif
	int	sf_signum;		/* signum argument for handler */
	int	sf_code;		/* code argument (FPU error code) */
	struct	sigcontext *sf_scp;	/* pointer to sigcontext */
#if defined(__minix)
	long	sf_fp;			/* saved frame pointer */
	long	sf_ra;			/* actual return address for handler */
	struct	sigcontext *sf_scpcopy;	/* copy of sf_scp */
#endif
	struct	sigcontext sf_sc;	/* actual saved context */
};

#endif /* _KERNEL || __minix */

#endif /* _AMD64_FRAME_H_ */
