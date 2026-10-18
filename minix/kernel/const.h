/* General macros and constants used by the kernel. */
#ifndef CONST_H
#define CONST_H

#include <minix/config.h>
#include <minix/bitmap.h>

#include "debug.h"

/* Translate an endpoint number to a process number, return success. */
#ifndef isokendpt
#define isokendpt(e,p) isokendpt_d((e),(p),0)
#define okendpt(e,p)   isokendpt_d((e),(p),1)
#endif

/* Constants used in virtual_copy(). Values must be 0 and 1, respectively. */
#define _SRC_	0
#define _DST_	1

#define get_sys_bit(map,bit) \
	( MAP_CHUNK((map).chunk,bit) & (1 << CHUNK_OFFSET(bit) ))
#define get_sys_bits(map,bit) \
	( MAP_CHUNK((map).chunk,bit) )
#define set_sys_bit(map,bit) \
	( MAP_CHUNK((map).chunk,bit) |= (1 << CHUNK_OFFSET(bit) ))
#define unset_sys_bit(map,bit) \
	( MAP_CHUNK((map).chunk,bit) &= ~(1 << CHUNK_OFFSET(bit) ))

/* for kputc() */
#define END_OF_KMESS	0

/* User limits. */
#ifndef USR_DATATOP
#ifndef _MINIX_MAGIC
#ifdef __x86_64__
/*
 * A process may have the user half of the address space, 128 TB, less its
 * top page (so that SYSRET never returns to the edge of the canonical range)
 * and less 896 MB-1 GB, where every address space has the kernel's device
 * mappings and the page directory windows (PD 0 from pg_mapkernel()'s
 * freepde_start = 448; VM keeps processes out of it, VM_HOLE_* in
 * servers/vm/vm.h).
 */
#define USR_DATATOP 0x00007ffffffff000UL
/*
 * Two stack tops.  The low one (kinfo.user_sp) is for VM, which the kernel
 * loads into its boot page tables (0-1 GB), the other boot processes and the
 * system services RS starts: their layout stays as it was, in the first GB.
 * The high one (kuserinfo.kui_user_sp, minix_get_user_sp()) is for the rest:
 * what libc's execve() builds a stack frame for, and VFS then places the
 * process's stack, and VM its mmap()s, accordingly.
 */
#define USR_STACKTOP 0x36000000			/* 864 MB */
#define USR_EXEC_STACKTOP 0x00007ff000000000UL
#else
#define USR_DATATOP 0xF0000000
#endif
#else
#define USR_DATATOP 0xE0000000	/* TODO: is this necessary? */
#endif
#endif

#ifndef USR_STACKTOP
#define USR_STACKTOP USR_DATATOP
#endif

#ifndef USR_DATATOP_COMPACT
#define USR_DATATOP_COMPACT USR_DATATOP
#endif

#ifndef USR_STACKTOP_COMPACT
#define USR_STACKTOP_COMPACT 0x50000000
#endif

#endif /* CONST_H */
