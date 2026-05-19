/* The kernel call implemented in this file:
 *   m_type:	SYS_VMCTL
 *
 * The parameters for this kernel call are:
 *   	SVMCTL_WHO	which process
 *    	SVMCTL_PARAM	set this setting (VMCTL_*)
 *    	SVMCTL_VALUE	to this value
 */

#include "kernel/system.h"
#include <assert.h>

#include "include/arch_proto.h"

extern phys_bytes video_mem_vaddr;

extern char *video_mem;

static void setcr3(struct proc *p, u64_t cr3, u64_t *v)
{
	/* Set process CR3 (64-bit page-table root). */
	p->p_seg.p_cr3 = cr3;
	assert(p->p_seg.p_cr3);
	p->p_seg.p_cr3_v = v;
	if (p == get_cpulocal_var(ptproc)) {
		write_cr3(p->p_seg.p_cr3);
	}
	if (p->p_nr == VM_PROC_NR) {
		if (arch_enable_paging(p) != OK)
			panic("arch_enable_paging failed");
	}
	RTS_UNSET(p, RTS_VMINHIBIT);
}

/*===========================================================================*
 *				arch_do_vmctl				     *
 *===========================================================================*/
int arch_do_vmctl(
  register message *m_ptr,	/* pointer to request message */
  struct proc *p
)
{
  /* Diagnostic: dump bytes at phys 0x3ffe2335 via the kernel's linear
   * physmap (pml4[256]) once per 200 vmctls.  Identity-map dump removed —
   * pml4[0] gets torn down for some virt ranges during boot setup and the
   * read triggers a kernel pagefault. */
  {
	static unsigned vmctl_dump_count = 0;
	if ((++vmctl_dump_count) % 200 == 1) {
		const volatile unsigned char *bp =
		    (const volatile unsigned char *)
		    (((char *)0xffff800000000000UL) + 0x3ffe2335UL);
		int i;
		printf("KDMP@vmctl#%u physmap:", vmctl_dump_count);
		for (i = 0; i < 16; i++)
			printf(" %02x", bp[i]);
		printf("\n");
	}
  }
  switch(m_ptr->SVMCTL_PARAM) {
	case VMCTL_GET_PDBR:
		/* Get process page directory base reg (CR3). */
		m_ptr->SVMCTL_VALUE = (u32_t) p->p_seg.p_cr3;
		return OK;
	case VMCTL_SETADDRSPACE:
		setcr3(p, (u64_t) m_ptr->SVMCTL_PTROOT,
		           (u64_t *) m_ptr->SVMCTL_PTROOT_V);
		return OK;
	case VMCTL_FLUSHTLB:
		reload_cr3();
		return OK;
	case VMCTL_I386_INVLPG:
		/* Reuse the i386 constant; amd64 implements it via INVLPG. */
		amd64_invlpg(m_ptr->SVMCTL_VALUE);
		return OK;
  }

  printf("arch_do_vmctl: strange param %d\n", m_ptr->SVMCTL_PARAM);
  return EINVAL;
}
