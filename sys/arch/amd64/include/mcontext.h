/*	$NetBSD: mcontext.h (Minix amd64 port)	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _AMD64_MCONTEXT_H_
#define _AMD64_MCONTEXT_H_

/*
 * mcontext extensions to handle signal delivery.
 */
#define _UC_SETSTACK	0x00010000
#define _UC_CLRSTACK	0x00020000
#define _UC_VM		0x00040000
#define	_UC_TLSBASE	0x00080000

/*
 * General register state for x86-64.
 * Indices match Minix's stackframe_s layout (see stackframe.h):
 *   gs, fs, es, ds, r15..r8, rbp, rdi, rsi, rdx, rcx, rbx, rax,
 *   retreg, pc, cs, psw, sp, ss
 */
#define _NGREG		25

typedef	long		__greg_t;
typedef	__greg_t	__gregset_t[_NGREG];

#define _REG_GS		0
#define _REG_FS		1
#define _REG_ES		2
#define _REG_DS		3
#define _REG_R15	4
#define _REG_R14	5
#define _REG_R13	6
#define _REG_R12	7
#define _REG_R11	8
#define _REG_R10	9
#define _REG_R9		10
#define _REG_R8		11
#define _REG_RBP	12
#define _REG_RDI	13
#define _REG_RSI	14
#define _REG_RDX	15
#define _REG_RCX	16
#define _REG_RBX	17
#define _REG_RAX	18
#define _REG_RETREG	19	/* return value register (Minix-specific) */
#define _REG_RIP	20	/* instruction pointer (PC) */
#define _REG_CS		21
#define _REG_RFLAGS	22	/* flags (PSW) */
#define _REG_RSP	23	/* stack pointer */
#define _REG_URSP	23	/* user stack pointer (alias for _REG_RSP) */
#define _REG_SS		24

/*
 * Floating-point register state (x87 + SSE/AVX)
 * We use the FXSAVE/FXRSTOR layout (512 bytes, 16-byte aligned).
 */
#define _FPREG_BYTES	512
typedef struct {
	unsigned char	__fpu_state[_FPREG_BYTES];
} __fpregset_t;

typedef struct {
	__gregset_t	__gregs;
	__fpregset_t	__fpregs;
	__greg_t	_mc_tlsbase;	/* TLS base (for _lwp_makecontext) */
	unsigned int	mc_magic;
	unsigned int	mc_flags;
} mcontext_t;

#define MCF_MAGIC	0xd50cc0de	/* mcontext magic number */

/* Minix-compatible ucontext flags (match sys/sys/ucontext.h) */
#define _UC_FPU		0x08
#define _UC_IGNFPU	0x20000
#define _UC_IGNSIGM	0x40000

/* Accessor macros for machine context fields */
#define _UC_MACHINE_SP(uc)	((uc)->uc_mcontext.__gregs[_REG_RSP])
#define _UC_MACHINE_PC(uc)	((uc)->uc_mcontext.__gregs[_REG_RIP])
#define _UC_MACHINE_INTRV(uc)	((uc)->uc_mcontext.__gregs[_REG_RAX])

#if defined(__minix)
#define _UC_MACHINE_STACK(uc)		((uc)->uc_mcontext.__gregs[_REG_RSP])
#define _UC_MACHINE_SET_STACK(uc, sp)	_UC_MACHINE_STACK(uc) = (sp)
#define _UC_MACHINE_SET_PC(uc, pc)	_UC_MACHINE_PC(uc) = (pc)
#define _UC_MACHINE_RBP(uc)		((uc)->uc_mcontext.__gregs[_REG_RBP])
#define _UC_MACHINE_SET_RBP(uc, rbp)	_UC_MACHINE_RBP(uc) = (rbp)
#define _UC_MACHINE_R12(uc)		((uc)->uc_mcontext.__gregs[_REG_R12])
#define _UC_MACHINE_SET_R12(uc, r12)	_UC_MACHINE_R12(uc) = (r12)

int setmcontext(const mcontext_t *mcp);
int getmcontext(mcontext_t *mcp);
#endif /* __minix */

#endif /* !_AMD64_MCONTEXT_H_ */
