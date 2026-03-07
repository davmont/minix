#ifndef _AMD64_TYPES_H
#define _AMD64_TYPES_H

#include <minix/sys_config.h>
#include <machine/stackframe.h>
#include <machine/fpu.h>
#include <sys/cdefs.h>

struct segdesc_s {
  u16_t limit_low;
  u16_t base_low;
  u8_t base_middle;
  u8_t access;
  u8_t granularity;
  u8_t base_high;
} __attribute__((packed));

struct sys_segdesc_s {
  u16_t limit_low;
  u16_t base_low;
  u8_t base_middle;
  u8_t access;
  u8_t granularity;
  u8_t base_high;
  u32_t base_highest;
  u32_t reserved;
} __attribute__((packed));

struct gatedesc_s {
  u16_t offset_low;
  u16_t selector;
  u8_t ist;
  u8_t p_dpl_type;
  u16_t offset_middle;
  u32_t offset_high;
  u32_t reserved;
} __attribute__((packed));

struct desctableptr_s {
  u16_t limit;
  u64_t base;
} __attribute__((packed));

typedef struct segframe {
	reg_t	p_cr3;
	u64_t	*p_cr3_v;
	char	*fpu_state;
	int	p_kern_trap_style;
} segframe_t;

struct cpu_info {
	u8_t	vendor;
	u8_t	family;
	u8_t	model;
	u8_t	stepping;
	u32_t	freq;
	u32_t	flags[2];
};

typedef u64_t atomic_t;

#endif
