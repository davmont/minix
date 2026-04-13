#ifndef FPU_H
#define FPU_H

/*
 * x86-64 FPU state saved by FXSAVE (512 bytes, 16-byte aligned).
 * FXSAVE on x86-64 uses 64-bit RIP and RDP, and has 16 XMM registers.
 *
 * When XSAVE is in use the hardware appends an XSAVE header (64 bytes,
 * starting at offset 512) followed by extended state components (YMM,
 * AVX-512 masks, ZMM registers, …).  The legacy FXSAVE region at offset
 * 0–511 is layout-compatible in both cases.
 */
struct xfp_save {
	u16_t fp_control;	/* FPU control word */
	u16_t fp_status;	/* FPU status word */
	u16_t fp_tag;		/* FPU tag word (abridged) */
	u16_t fp_opcode;	/* last FPU opcode */
	u64_t fp_rip;		/* instruction pointer */
	u64_t fp_rdp;		/* data pointer */
	u32_t fp_mxcsr;		/* MXCSR */
	u32_t fp_mxcsr_mask;	/* MXCSR mask */
	u16_t fp_st_regs[8][8];	/* 128 bytes for x87 ST/MM registers */
	u32_t fp_xreg_word[64];	/* 256 bytes for 16 XMM registers */
	u32_t fp_padding[24];	/* padding to 512 bytes */
};

/*
 * FPU_XFP_SIZE — size of the FXSAVE-compatible legacy region (512 bytes).
 * This is also the size exposed to userspace via sigcontext / mcontext;
 * do not change it without updating the ABI.
 */
#define FPU_XFP_SIZE		512

/*
 * FPU_XSAVE_MAX_SIZE — maximum kernel-internal FPU save buffer.
 * 4096 bytes covers the legacy FXSAVE area (512), the XSAVE header (64),
 * and all current Intel/AMD extended state components including AVX-512
 * ZMM registers (~2696 bytes on Alder Lake), with room for future growth.
 * The actual bytes used per process is determined at boot by querying
 * CPUID leaf 0xD and stored in xsave_area_size (arch_system.c).
 */
#define FPU_XSAVE_MAX_SIZE	4096

/*
 * FPUALIGN — required buffer alignment.
 * XSAVE requires 64-byte alignment; FXSAVE only requires 16 bytes.
 * Using 64 satisfies both.
 */
#define FPUALIGN		64

union fpu_state_u {
	struct xfp_save xfp_regs;
};

#endif /* FPU_H */
