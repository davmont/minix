/*	$NetBSD: shell.h,v 1.18 2013/04/28 17:01:28 dholland Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)shell.h	8.2 (Berkeley) 5/4/95
 */

/*
 * The follow should be set to reflect the type of system you have:
 *	JOBS -> 1 if you have Berkeley job control, 0 otherwise.
 *	SHORTNAMES -> 1 if your linker cannot handle long names.
 *	define BSD if you are running 4.2 BSD or later.
 *	define SYSV if you are running under System V.
 *	define DEBUG=1 to compile in debugging ('set -o debug' to turn on)
 *	define DEBUG=2 to compile in and turn on debugging.
 *	define DO_SHAREDVFORK to indicate that vfork(2) shares its address
 *	       with its parent.
 *
 * When debugging is on, debugging info will be written to ./trace and
 * a quit signal will generate a core dump.
 */

#include <sys/param.h>

#if defined(__minix)
#define JOBS 0
#else
#define JOBS 1
#endif /* defined(__minix) */
#ifndef BSD
#define BSD 1
#endif

#ifndef DO_SHAREDVFORK
#if !defined(__minix)
#if __NetBSD_Version__ >= 104000000
#define DO_SHAREDVFORK
#endif
#endif /* !defined(__minix) */
#endif

typedef void *pointer;
#ifndef NULL
#define NULL (void *)0
#endif
#define STATIC	/* empty */
#define MKINIT	/* empty */

#include <sys/cdefs.h>

extern const char nullstr[1];		/* null string */


#ifdef DEBUG
#define TRACE(param)	trace param
#define TRACEV(param)	tracev param
#define CTRACE(n, param) do { if (debug && (n)) trace param; } while (0)
#define VTRACE(n, param) do { if (debug && (n)) trace param; } while (0)
#define CVTRACE(n, c, p) do { if (debug && (n) && (c)) trace p; } while (0)
#define XTRACE(n, p, t)  do { if (debug && (n)) { trace p; t; } } while (0)
#define VXTRACE(n, p, t) do { if (debug && (n)) { trace p; t; } } while (0)
#else
#define TRACE(param)	((void)0)
#define TRACEV(param)	((void)0)
#define CTRACE(n, param) ((void)0)
#define VTRACE(n, param) ((void)0)
#define CVTRACE(n, c, p) ((void)0)
#define XTRACE(n, p, t)  ((void)0)
#define VXTRACE(n, p, t) ((void)0)
#endif

#define DBG_ALWAYS	0x0001
#define DBG_ERRS	0x0002
#define DBG_PROCS	0x0004
#define DBG_CMDS	0x0008
#define DBG_EXPAND	0x0010
#define DBG_PARSE	0x0020
#define DBG_LEXER	0x0040
#define DBG_REDIR	0x0080
#define DBG_TRAP	0x0100
#define DBG_SIG		0x0200
#define DBG_VARS	0x0400
#define DBG_OUTPUT	0x0800
#define DBG_HISTORY	0x1000
#define DBG_MATCH	0x2000
#define DBG_INPUT	0x4000
#define DBG_EV		0x8000
