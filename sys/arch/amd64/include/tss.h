/*	$NetBSD: tss.h (adapted for amd64/x86_64)	*/

#ifndef _AMD64_TSS_H_
#define _AMD64_TSS_H_

/*
 * x86_64 Task State Segment (64-bit TSS format per Intel SDM).
 * The 64-bit TSS is used only for RSP0/RSP1/RSP2 and IST stack pointers;
 * the general-purpose register fields are not used for hardware task switching.
 */
struct x86_64_tss {
	uint32_t	tss_reserved0;
	uint64_t	tss_rsp0;	/* kernel stack pointer (CPL 0) */
	uint64_t	tss_rsp1;
	uint64_t	tss_rsp2;
	uint64_t	tss_reserved1;
	uint64_t	tss_ist[7];	/* IST stack pointers (ist1..ist7) */
	uint64_t	tss_reserved2;
	uint16_t	tss_reserved3;
	uint16_t	tss_iobase;	/* I/O permission bitmap base */
} __attribute__((packed));

typedef struct x86_64_tss x86_64tss_t;

#define	IOMAP_INVALOFF	0xffff

#endif /* _AMD64_TSS_H_ */
