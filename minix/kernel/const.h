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
 * On amd64 the VM server uses a single PD covering 0-1 GB.  pg_mapkernel()
 * returns freepde_start = 448, reserving PD[448..511] (896 MB-1 GB) for VM's
 * device and pagedir mappings; PD[0..447] (0-896 MB) is user virtual space.
 * The 896 MB ceiling lets a large executable (e.g. clang, ~156 MB text) plus
 * its libraries and stack all fit (the old 512 MB / 128 MB-stack layout could
 * not).  Keep this in sync with pg_mapkernel()'s return value.
 */
#define USR_DATATOP 0x38000000		/* 896 MB */
/*
 * Stack top at 864 MB, just below USR_DATATOP and above the mmap region (see
 * VM_MMAPTOP in servers/vm/vm.h).  NOTE: the kernel sets the *initial* VM
 * server stack here too, so it must be within physical RAM at boot -- fine for
 * the >=1 GB configurations needed to run a self-hosting toolchain, but this
 * raises the previous 128 MB value that supported tiny (-m 256) configs.
 */
#define USR_STACKTOP 0x36000000		/* 864 MB */
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
