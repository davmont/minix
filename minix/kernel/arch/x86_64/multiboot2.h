/*
 * multiboot2.h - minimal Multiboot2 boot-information definitions.
 *
 * These structures and constants follow the public "Multiboot2
 * Specification" (the boot interface published by the GNU project).  They
 * describe an interface, and are written fresh here from the specification
 * rather than copied from GRUB's GPL sources, so this header keeps the
 * project's existing BSD-style licensing.
 *
 * Only the subset the amd64 kernel consumes is defined: the boot magic, the
 * information header, and the cmdline / module / memory-map / framebuffer /
 * ACPI-RSDP tags.  See minix/kernel/arch/x86_64/pre_init.c (mb2_to_mb1) and
 * docs/UEFI_BOOT.md.
 */

#ifndef _MINIX_KERNEL_MULTIBOOT2_H
#define _MINIX_KERNEL_MULTIBOOT2_H

#include <machine/types.h>

/* Value the bootloader leaves in EAX when it hands off via Multiboot2. */
#define MULTIBOOT2_BOOTLOADER_MAGIC	0x36d76289

/* Magic in the Multiboot2 OS-image header (placed in the .multiboot2 section
 * by head.S). */
#define MULTIBOOT2_HEADER_MAGIC		0xe85250d6

/* All tags (header and information) are padded up to an 8-byte boundary. */
#define MULTIBOOT2_TAG_ALIGN		8

/* Information tag types (mb2 boot information). */
#define MULTIBOOT2_TAG_END		0
#define MULTIBOOT2_TAG_CMDLINE		1
#define MULTIBOOT2_TAG_BOOT_LOADER_NAME	2
#define MULTIBOOT2_TAG_MODULE		3
#define MULTIBOOT2_TAG_BASIC_MEMINFO	4
#define MULTIBOOT2_TAG_MMAP		6
#define MULTIBOOT2_TAG_FRAMEBUFFER	8
#define MULTIBOOT2_TAG_ACPI_OLD		14	/* RSDP v1 (20 bytes) */
#define MULTIBOOT2_TAG_ACPI_NEW		15	/* RSDP v2 (>= 20 bytes) */

/* Memory-map entry type (matches Multiboot1 values). */
#define MULTIBOOT2_MEMORY_AVAILABLE	1

/* Framebuffer type. */
#define MULTIBOOT2_FRAMEBUFFER_TYPE_RGB	1

/* Header of the whole information block (EBX points here). */
struct multiboot2_info {
	u32_t	total_size;	/* total size of the information block */
	u32_t	reserved;	/* always 0 */
	/* followed by a sequence of 8-byte-aligned tags */
};

/* Common header of every information tag. */
struct multiboot2_tag {
	u32_t	type;
	u32_t	size;		/* size including this header, excluding padding */
};

/* type 1 (cmdline) / type 2 (loader name): NUL-terminated string follows. */
struct multiboot2_tag_string {
	u32_t	type;
	u32_t	size;
	char	string[1];
};

/* type 3: a boot module. */
struct multiboot2_tag_module {
	u32_t	type;
	u32_t	size;
	u32_t	mod_start;	/* physical start address */
	u32_t	mod_end;	/* physical end address */
	char	cmdline[1];	/* NUL-terminated module command line */
};

/* type 6: memory map.  entry_size bytes per entry, may exceed the struct. */
struct multiboot2_mmap_entry {
	u64_t	addr;
	u64_t	len;
	u32_t	type;
	u32_t	zero;
};
struct multiboot2_tag_mmap {
	u32_t	type;
	u32_t	size;
	u32_t	entry_size;
	u32_t	entry_version;
	struct multiboot2_mmap_entry entries[1];
};

/* type 8: framebuffer information. */
struct multiboot2_tag_framebuffer {
	u32_t	type;
	u32_t	size;
	u64_t	framebuffer_addr;
	u32_t	framebuffer_pitch;
	u32_t	framebuffer_width;
	u32_t	framebuffer_height;
	u8_t	framebuffer_bpp;
	u8_t	framebuffer_type;
	u16_t	reserved;
	/* colour-info follows; not needed for a 32-bpp RGB linear console */
};

/* type 14/15: a verbatim copy of the ACPI RSDP. */
struct multiboot2_tag_acpi {
	u32_t	type;
	u32_t	size;
	u8_t	rsdp[1];	/* copy of the RSDP bytes */
};

#endif /* _MINIX_KERNEL_MULTIBOOT2_H */
