/*	$NetBSD: kcore.h (amd64 port)	*/

#ifndef _AMD64_KCORE_H_
#define _AMD64_KCORE_H_

typedef struct cpu_kcore_hdr {
	uint64_t	ptdpaddr;		/* PA of PML4 table */
	uint32_t	nmemsegs;		/* Number of RAM segments */
#if 0
	phys_ram_seg_t  memsegs[];		/* RAM segments */
#endif
} cpu_kcore_hdr_t;

#ifdef _KERNEL
void	dumpsys(void);
extern int	sparse_dump;
#endif

#endif /* _AMD64_KCORE_H_ */
