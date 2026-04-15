/*	$NetBSD: pte.h (amd64 port)	*/

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _AMD64_PTE_H_
#define _AMD64_PTE_H_

/*
 * x86-64 MMU uses a 4-level page table.
 */

#if !defined(_LOCORE)
typedef uint64_t pd_entry_t;		/* PDE */
typedef uint64_t pt_entry_t;		/* PTE */
#endif

/*
 * Page table level shifts and masks.
 * L1 = PTE, L2 = PDE (2MB large pages), L3 = PDPTE (1GB), L4 = PML4E
 */
#define	L1_SHIFT	12
#define	L2_SHIFT	21
#define	L3_SHIFT	30
#define	L4_SHIFT	39

#define	NBPD_L1		(1UL << L1_SHIFT)	/* 4KB */
#define	NBPD_L2		(1UL << L2_SHIFT)	/* 2MB */
#define	NBPD_L3		(1UL << L3_SHIFT)	/* 1GB */
#define	NBPD_L4		(1UL << L4_SHIFT)	/* 512GB */

#define	L4_MASK		0x0000ff8000000000UL
#define	L3_MASK		0x0000007fc0000000UL
#define	L2_MASK		0x000000003fe00000UL
#define	L1_MASK		0x00000000001ff000UL

/*
 * Number of entries per page table page.
 */
#define	L1_NENTRIES	512
#define	L2_NENTRIES	512
#define	L3_NENTRIES	512
#define	L4_NENTRIES	512

/*
 * Page index macros — extract page-table index from a virtual address.
 */
#define	pl1_pi(va)	(((va) >> L1_SHIFT) & 0x1ff)
#define	pl2_pi(va)	(((va) >> L2_SHIFT) & 0x1ff)
#define	pl3_pi(va)	(((va) >> L3_SHIFT) & 0x1ff)
#define	pl4_pi(va)	(((va) >> L4_SHIFT) & 0x1ff)

/*
 * PTE / PDE bit definitions.
 */
#define	PG_V		0x0000000000000001UL	/* valid (present) */
#define	PG_RO		0x0000000000000000UL	/* read-only */
#define	PG_RW		0x0000000000000002UL	/* read-write */
#define	PG_u		0x0000000000000004UL	/* user-accessible */
#define	PG_PROT		0x0000000000000806UL
#define	PG_WT		0x0000000000000008UL	/* write-through */
#define	PG_N		0x0000000000000010UL	/* non-cacheable */
#define	PG_U		0x0000000000000020UL	/* accessed */
#define	PG_M		0x0000000000000040UL	/* modified (dirty) */
#define	PG_PAT		0x0000000000000080UL	/* PAT (on PTE) */
#define	PG_PS		0x0000000000000080UL	/* large page (on PDE) */
#define	PG_G		0x0000000000000100UL	/* global */
#define	PG_AVAIL1	0x0000000000000200UL
#define	PG_AVAIL2	0x0000000000000400UL
#define	PG_AVAIL3	0x0000000000000800UL
#define	PG_LGPAT	0x0000000000001000UL	/* PAT on large pages */
#define	PG_NX		0x8000000000000000UL	/* no-execute */

/*
 * Page frame masks.
 */
#define	PG_FRAME	0x000ffffffffff000UL	/* 4KB page frame */
#define	PG_LGFRAME	0x000fffffffe00000UL	/* 2MB page frame */
#define	PG_2MFRAME	0x000fffffffe00000UL	/* 2MB page frame (alias) */
#define	PG_1GFRAME	0x000fffffc0000000UL	/* 1GB page frame */

/*
 * Protection codes.
 */
#define	PG_KR		0x0000000000000000UL	/* kernel read-only */
#define	PG_KW		0x0000000000000002UL	/* kernel read-write */

/*
 * Sign extension helper: canonical-form virtual addresses on x86-64.
 * Bits 63:48 must equal bit 47.
 */
#define	VA_SIGN_NEG(va)		((va) | 0xffff000000000000UL)
#define	VA_SIGN_POS(va)		((va) & 0x0000ffffffffffffUL)

#include <x86/pte.h>

#endif /* _AMD64_PTE_H_ */
