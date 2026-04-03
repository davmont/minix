
#ifndef _AMD64_PROTO_H
#define _AMD64_PROTO_H

#include <machine/vm.h>

#define K_STACK_SIZE    AMD64_PAGE_SIZE

#ifndef __ASSEMBLY__

/* Hardware interrupt handlers (mpx.S). */
void hwint00(void);
void hwint01(void);
void hwint02(void);
void hwint03(void);
void hwint04(void);
void hwint05(void);
void hwint06(void);
void hwint07(void);
void hwint08(void);
void hwint09(void);
void hwint10(void);
void hwint11(void);
void hwint12(void);
void hwint13(void);
void hwint14(void);
void hwint15(void);

/* Exception handlers (mpx.S). */
void divide_error(void);
void single_step_exception(void);
void nmi(void);
void breakpoint_exception(void);
void overflow(void);
void bounds_check(void);
void inval_opcode(void);
void copr_not_available(void);
void double_fault(void);
void copr_seg_overrun(void);
void inval_tss(void);
void segment_not_present(void);
void stack_exception(void);
void general_protection(void);
void page_fault(void);
void copr_error(void);
void alignment_check(void);
void machine_check(void);
void simd_exception(void);

/* Kernel entry points (mpx.S). */
void ipc_entry_softint_orig(void);
void ipc_entry_softint_um(void);
void syscall_entry(void);
void kernel_call_entry_orig(void);
void kernel_call_entry_um(void);

/* Context restore (mpx.S). */
void restore_user_context_int(struct proc *);
void restore_user_context_syscall(struct proc *);

/*
 * 64-bit TSS structure.
 * In long mode the TSS is used only for IST stack pointers and the
 * I/O permission bitmap base; hardware task switching is gone.
 */
struct tss_s {
	u32_t  reserved0;
	u64_t  rsp0;        /* kernel stack pointer (ring 0) */
	u64_t  rsp1;
	u64_t  rsp2;
	u64_t  reserved1;
	/* Interrupt Stack Table: IST[1..7] */
	u64_t  ist[8];      /* ist[0] unused; ist[1..7] are IST1..IST7 */
	u64_t  reserved2;
	u16_t  reserved3;
	u16_t  iobase;      /* I/O permission bitmap offset */
} __attribute__((packed));

/* exception.c */
struct exception_frame {
	reg_t  vector;
	reg_t  errcode;
	reg_t  rip;
	reg_t  cs;
	reg_t  rflags;
	reg_t  rsp;
	reg_t  ss;
};

void exception_handler(int is_nested, struct exception_frame *frame);

/* protect.c */
int  tss_init(unsigned cpu, void *kernel_stack);
void prot_init(void);
void prot_load_selectors(void);
void idt_init(void);
void idt_reload(void);
void int_gate_idt(unsigned vec_nr, vir_bytes offset, unsigned dpl_type);

struct gate_table_s {
	void (*gate)(void);
	unsigned char vec_nr;
	unsigned char privilege;
};

void idt_copy_vectors(struct gate_table_s *first);
void idt_copy_vectors_pic(void);

/* klib.S */
void   amd64_invlpg(vir_bytes addr);
phys_bytes phys_memset(phys_bytes dst, u64_t pattern, phys_bytes count);
void   memset_fault(void);
void   memset_fault_in_kernel(void);
__dead void x86_triplefault(void);
reg_t  read_cr0(void);
reg_t  read_cr2(void);
reg_t  read_cr3(void);
reg_t  read_cr4(void);
void   write_cr0(reg_t value);
void   write_cr3(reg_t value);
void   write_cr4(reg_t value);
void   reload_cr3(void);
reg_t  read_cpu_flags(void);
void   ia32_msr_read(u32_t reg, u32_t *hi, u32_t *lo);
void   ia32_msr_write(u32_t reg, u32_t hi, u32_t lo);
void   x86_lgdt(void *);
void   x86_lidt(void *);
void   x86_ltr(u32_t);
void   x86_load_kerncs(void);
void   x86_load_ds(u32_t);
void   x86_load_ss(u32_t);
void   x86_load_es(u32_t);
void   x86_load_fs(u32_t);
void   x86_load_gs(u32_t);
void   fninit(void);
void   clts(void);
void   fxsave(void *);
int    fxrstor(void *);
void   mfence(void);

/* pg_utils.c */
void       pg_clear(void);
void       pg_identity(kinfo_t *cbi);
int        pg_mapkernel(void);
phys_bytes pg_load(void);
void       pg_map(phys_bytes phys, vir_bytes vaddr, vir_bytes vaddr_end,
                  kinfo_t *cbi);
void       pg_info(reg_t *pagedir_ph, u64_t **pagedir_v);
phys_bytes pg_roundup(phys_bytes b);
void       vm_enable_paging(void);
void       add_memmap(kinfo_t *cbi, u64_t addr, u64_t len);
phys_bytes alloc_lowest(kinfo_t *cbi, phys_bytes len);
void       cut_memmap(kinfo_t *cbi, phys_bytes start, phys_bytes end);

/* multiboot / pre_init */
void multiboot_init(void);

/* protect.c helpers also used by arch_system.c */
void enable_iop(struct proc *pp);
phys_bytes vir2phys(void *vir);

extern void *k_stacks_start;
extern void *k_stacks;

#define get_k_stack_top(cpu) \
	((void *)(((char *)(k_stacks)) + 2 * ((cpu) + 1) * K_STACK_SIZE))

#define barrier() do { mfence(); } while (0)

#define get_stack_frame(__X) ((reg_t)__builtin_frame_address(0))

int tss_init(unsigned cpu, void *kernel_stack);
void int_gate_idt(unsigned vec_nr, vir_bytes offset, unsigned dpl_type);

/* functions defined in architecture-independent kernel source */
#include "kernel/proto.h"

#endif /* __ASSEMBLY__ */

#endif /* _AMD64_PROTO_H */
