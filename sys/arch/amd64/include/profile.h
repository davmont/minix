/*	$NetBSD: profile.h,v 1.33 2007/12/20 23:46:13 ad Exp $ (adapted for amd64)	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _AMD64_PROFILE_H_
#define _AMD64_PROFILE_H_

#ifdef _KERNEL_OPT
#include "opt_multiprocessor.h"
#endif

#ifdef _KERNEL
#include <machine/cpufunc.h>
#include <machine/lock.h>
#endif

#define	_MCOUNT_DECL static __inline void _mcount

#ifdef __ELF__
#define MCOUNT_ENTRY	"__mcount"
#define MCOUNT_COMPAT	__weak_alias(mcount, __mcount)
#else
#define MCOUNT_ENTRY	"mcount"
#define MCOUNT_COMPAT	/* nothing */
#endif

/*
 * x86-64 mcount: save caller-save registers (rdi, rsi, rdx, rcx, r8, r9,
 * rax), get frompc from parent frame's return address, selfpc from our
 * return address, then call _mcount.
 */
#define	MCOUNT \
MCOUNT_COMPAT								\
extern void mcount(void) __asm(MCOUNT_ENTRY)				\
	__attribute__((__no_instrument_function__));			\
void									\
mcount(void)								\
{									\
	u_long selfpc, frompcindex;					\
	__asm volatile("movq 8(%%rbp),%0" : "=r" (selfpc));		\
	__asm volatile("movq (%%rbp),%0;"				\
	               "movq 8(%0),%0"					\
	    : "=r" (frompcindex));					\
	_mcount(frompcindex, selfpc);					\
}

#ifdef _KERNEL
#ifdef MULTIPROCESSOR
__cpu_simple_lock_t __mcount_lock;

static inline void
MCOUNT_ENTER_MP(void)
{
	__cpu_simple_lock(&__mcount_lock);
	__insn_barrier();
}

static inline void
MCOUNT_EXIT_MP(void)
{
	__insn_barrier();
	__mcount_lock = __SIMPLELOCK_UNLOCKED;
}
#else
#define MCOUNT_ENTER_MP()
#define MCOUNT_EXIT_MP()
#endif

static inline void
mcount_disable_intr(void)
{
	__asm volatile("cli");
}

static inline u_long
mcount_read_psl(void)
{
	u_long ef;
	__asm volatile("pushfq; popq %0" : "=r" (ef));
	return ef;
}

static inline void
mcount_write_psl(u_long ef)
{
	__asm volatile("pushq %0; popfq" : : "r" (ef));
}

#define	MCOUNT_ENTER							\
	s = (int)mcount_read_psl();					\
	mcount_disable_intr();						\
	MCOUNT_ENTER_MP();

#define	MCOUNT_EXIT							\
	MCOUNT_EXIT_MP();						\
	mcount_write_psl(s);

#endif /* _KERNEL */

#endif /* _AMD64_PROFILE_H_ */
