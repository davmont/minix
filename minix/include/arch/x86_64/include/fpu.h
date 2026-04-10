#ifndef FPU_H
#define FPU_H

/*
 * x86-64 FPU state saved by FXSAVE (512 bytes, 16-byte aligned).
 * FXSAVE on x86-64 uses 64-bit RIP and RDP, and has 16 XMM registers.
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

/* Size of xfp_save structure (FXSAVE output). */
#define FPU_XFP_SIZE	512

/* Required alignment for FXSAVE/FXRSTOR. */
#define FPUALIGN	16

union fpu_state_u {
	struct xfp_save xfp_regs;
};

#endif /* FPU_H */
