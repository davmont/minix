/*
 * pg_utils.c - amd64 page-table management (4-level paging: PML4/PDPT/PD/PT).
 *
 * Virtual address breakdown (48-bit canonical, 4 KB pages):
 *
 *   [63:48]  sign-extended (canonical form)
 *   [47:39]  PML4 index  (9 bits, 512 entries)
 *   [38:30]  PDPT index  (9 bits, 512 entries)
 *   [29:21]  PD index    (9 bits, 512 entries, or 2 MB page if PS set)
 *   [20:12]  PT index    (9 bits, 512 entries)
 *   [11:0]   Page offset (12 bits)
 *
 * During early boot we use 2 MB (large) pages in the PD to identity-map the
 * first 4 GB without needing PT tables.  The kernel itself is then mapped at
 * its high virtual address using the same scheme.
 *
 * All page-table entries use 64-bit (u64_t) entries.
 */

#include <assert.h>
#include <string.h>

#include "kernel/kernel.h"
#include "include/arch_proto.h"
#include "include/archconst.h"

/* AMD64 virtual-address field shifts. */
#define AMD64_PML4_SHIFT    39
#define AMD64_PDPT_SHIFT    30
#define AMD64_PD_SHIFT      21
#define AMD64_PT_SHIFT      12

#define AMD64_ENTRIES       512     /* entries per table */
#ifndef AMD64_PAGE_SIZE
#define AMD64_PAGE_SIZE     4096UL
#endif
#ifndef AMD64_BIG_PAGE_SIZE
#define AMD64_BIG_PAGE_SIZE (2UL * 1024 * 1024)    /* 2 MB */
#endif
#ifndef AMD64_HUGE_PAGE_SIZE
#define AMD64_HUGE_PAGE_SIZE (1UL * 1024 * 1024 * 1024) /* 1 GB */
#endif

/* Page-table entry flags. */
#define PG_PRESENT  (1ULL << 0)
#define PG_WRITE    (1ULL << 1)
#define PG_USER     (1ULL << 2)
#define PG_PWT      (1ULL << 3)     /* write-through */
#define PG_PCD      (1ULL << 4)     /* cache disable */
#define PG_PS       (1ULL << 7)     /* page size (2MB in PD, 1GB in PDPT) */
#define PG_GLOBAL   (1ULL << 8)
#define PG_NX       (1ULL << 63)   /* no-execute (requires EFER.NXE) */

#define PG_ADDR_MASK    0x000FFFFFFFFFF000ULL

/* Forward declarations */
phys_bytes pg_alloc_page(kinfo_t *cbi);

/* Kernel virtual and physical base addresses (from kernel.lds). */
extern char _kern_vir_base, _kern_phys_base, _kern_size;

static phys_bytes kern_vir_start  = (phys_bytes)&_kern_vir_base;
static phys_bytes kern_phys_start = (phys_bytes)&_kern_phys_base;
static phys_bytes kern_kernlen    = (phys_bytes)&_kern_size;

/*
 * Root PML4 table (one per address space; this is the bootstrap copy).
 * During early boot the kernel runs with this table; VM sets up its own later.
 */
static u64_t pml4[AMD64_ENTRIES] __aligned(AMD64_PAGE_SIZE);

/* =========================================================================
 * Small static pool of pre-allocated page tables for early-boot use.
 * After VM is up, all allocation goes through VM's physical allocator.
 * ========================================================================= */

#define BOOT_PT_POOL    16  /* enough for the first 4 GB identity map */

static u64_t boot_pt_pool[BOOT_PT_POOL][AMD64_ENTRIES]
    __aligned(AMD64_PAGE_SIZE);
static int boot_pt_used = 0;

static u64_t *alloc_pagetable(phys_bytes *phys_out)
{
    u64_t *pt;
    assert(boot_pt_used < BOOT_PT_POOL);
    pt = boot_pt_pool[boot_pt_used++];
    memset(pt, 0, AMD64_PAGE_SIZE);
    *phys_out = vir2phys(pt);
    return pt;
}

/* =========================================================================
 * Index extraction
 * ========================================================================= */

static inline int pml4_idx(vir_bytes va) { return (int)((va >> AMD64_PML4_SHIFT) & 0x1FF); }
static inline int pdpt_idx(vir_bytes va) { return (int)((va >> AMD64_PDPT_SHIFT) & 0x1FF); }
static inline int pd_idx  (vir_bytes va) { return (int)((va >> AMD64_PD_SHIFT  ) & 0x1FF); }
static inline int pt_idx  (vir_bytes va) { return (int)((va >> AMD64_PT_SHIFT  ) & 0x1FF); }

/* =========================================================================
 * pg_clear — zero out the root PML4
 * ========================================================================= */

void pg_clear(void)
{
    memset(pml4, 0, sizeof(pml4));
    boot_pt_used = 0;   /* reclaim the pool for a fresh layout */
}

/* =========================================================================
 * pg_identity — identity-map the first ~4 GB using 2 MB pages.
 *
 * We only need PML4[0] → one PDPT → up to 4 PDs (each covering 1 GB).
 * Non-RAM regions (above cbi->mem_high_phys) are marked cache-disabled.
 * ========================================================================= */

void pg_identity(kinfo_t *cbi)
{
    phys_bytes ph;
    u64_t *pdpt, *pd;
    int i, j;
    phys_bytes mapped;
    u64_t flags;

    assert(cbi->mem_high_phys);

    /* Allocate the PDPT. */
    pdpt = alloc_pagetable(&ph);
    pml4[0] = (ph & PG_ADDR_MASK) | PG_PRESENT | PG_WRITE;

    /* Map 4 × 1 GB = 4 GB total using 2 MB pages per PD. */
    for (i = 0; i < 4; i++) {
        pd = alloc_pagetable(&ph);
        pdpt[i] = (ph & PG_ADDR_MASK) | PG_PRESENT | PG_WRITE;

        for (j = 0; j < AMD64_ENTRIES; j++) {
            mapped = ((phys_bytes)i * AMD64_HUGE_PAGE_SIZE)
                   + ((phys_bytes)j * AMD64_BIG_PAGE_SIZE);

            flags = PG_PRESENT | PG_WRITE | PG_USER | PG_PS;
            if (mapped >= (cbi->mem_high_phys & ~(AMD64_BIG_PAGE_SIZE - 1)))
                flags |= PG_PWT | PG_PCD;  /* non-RAM: cache disabled */

            pd[j] = (mapped & PG_ADDR_MASK) | flags;
        }
    }
}

/* =========================================================================
 * pg_mapkernel — map the kernel at its virtual address using 2 MB pages.
 * ========================================================================= */

int pg_mapkernel(void)
{
    vir_bytes va   = kern_vir_start;
    phys_bytes pa  = kern_phys_start;
    phys_bytes end = kern_vir_start + kern_kernlen;
    phys_bytes ph;

    assert(!(va  % AMD64_BIG_PAGE_SIZE));
    assert(!(pa  % AMD64_BIG_PAGE_SIZE));

    while (va < end) {
        int ml4i = pml4_idx(va);
        int dpti  = pdpt_idx(va);
        int pdi   = pd_idx(va);

        u64_t *pdpt, *pd;

        /* Ensure PML4 entry exists. */
        if (!(pml4[ml4i] & PG_PRESENT)) {
            pdpt = alloc_pagetable(&ph);
            pml4[ml4i] = (ph & PG_ADDR_MASK) | PG_PRESENT | PG_WRITE;
        } else {
            pdpt = (u64_t *)(uintptr_t)(pml4[ml4i] & PG_ADDR_MASK);
        }

        /* Ensure PDPT entry exists. */
        if (!(pdpt[dpti] & PG_PRESENT)) {
            pd = alloc_pagetable(&ph);
            pdpt[dpti] = (ph & PG_ADDR_MASK) | PG_PRESENT | PG_WRITE;
        } else {
            pd = (u64_t *)(uintptr_t)(pdpt[dpti] & PG_ADDR_MASK);
        }

        /* Map with a 2 MB (PS) page. */
        pd[pdi] = ((u64_t)pa & PG_ADDR_MASK) | PG_PRESENT | PG_WRITE | PG_PS;

        va  += AMD64_BIG_PAGE_SIZE;
        pa  += AMD64_BIG_PAGE_SIZE;
    }

    /*
     * Return a freepde_start value for the VM server's single PD (which covers
     * 0–1 GB).  PD[0..255] are reserved for user-space mappings (0–512 MB) and
     * PD[256..511] are available for kernel-device and pagedir mappings.
     */
    (void)pml4_idx(kern_vir_start); /* suppress unused-variable warning */
    return 256;
}

/* =========================================================================
 * pg_load — load CR3 with the PML4 physical address
 * ========================================================================= */

phys_bytes pg_load(void)
{
    phys_bytes ph = vir2phys(pml4);
    write_cr3((reg_t)ph);
    return ph;
}

/* =========================================================================
 * pg_map — map a virtual range to physical addresses (4 KB pages).
 * Used by the kernel to map VM and other boot modules into address space
 * before the VM server is running.
 * ========================================================================= */

static int mapped_pd_idx = -1;   /* last PD index that got a PT */
static u64_t *current_pt = NULL;

static phys_bytes pg_rounddown(phys_bytes b)
{
    phys_bytes o = b % AMD64_PAGE_SIZE;
    return o ? b - o : b;
}

void pg_map(phys_bytes phys, vir_bytes vaddr, vir_bytes vaddr_end,
            kinfo_t *cbi)
{
    assert(kernel_may_alloc);

    if (phys == PG_ALLOCATEME) {
        assert(!(vaddr % AMD64_PAGE_SIZE));
    } else {
        assert((vaddr % AMD64_PAGE_SIZE) == (phys % AMD64_PAGE_SIZE));
        vaddr = pg_rounddown(vaddr);
        phys  = pg_rounddown(phys);
    }
    assert(vaddr < kern_vir_start);

    while (vaddr < vaddr_end) {
        phys_bytes source = phys;
        phys_bytes ph;

        if (phys == PG_ALLOCATEME)
            source = pg_alloc_page(cbi);
        else
            assert(!(phys % AMD64_PAGE_SIZE));

        assert(!(source % AMD64_PAGE_SIZE));

        {
            int ml4i = pml4_idx(vaddr);
            int dpti  = pdpt_idx(vaddr);
            int pdi   = pd_idx(vaddr);
            int pti   = pt_idx(vaddr);
            u64_t *pdpt, *pd;

            /* PML4 */
            if (!(pml4[ml4i] & PG_PRESENT)) {
                pdpt = alloc_pagetable(&ph);
                pml4[ml4i] = (ph & PG_ADDR_MASK) | PG_PRESENT | PG_WRITE | PG_USER;
            } else {
                pml4[ml4i] |= PG_USER;
                pdpt = (u64_t *)(uintptr_t)(pml4[ml4i] & PG_ADDR_MASK);
            }

            /* PDPT */
            if (!(pdpt[dpti] & PG_PRESENT)) {
                pd = alloc_pagetable(&ph);
                pdpt[dpti] = (ph & PG_ADDR_MASK) | PG_PRESENT | PG_WRITE | PG_USER;
            } else {
                pdpt[dpti] |= PG_USER;
                pd = (u64_t *)(uintptr_t)(pdpt[dpti] & PG_ADDR_MASK);
            }

            /* PD — allocate a new PT if necessary. */
            if (mapped_pd_idx != (ml4i << 18 | dpti << 9 | pdi) ||
                current_pt == NULL) {
                current_pt = alloc_pagetable(&ph);
                pd[pdi] = (ph & PG_ADDR_MASK) | PG_PRESENT | PG_WRITE | PG_USER;
                mapped_pd_idx = (ml4i << 18 | dpti << 9 | pdi);
            }

            /* PT */
            current_pt[pti] = ((u64_t)source & PG_ADDR_MASK)
                            | PG_PRESENT | PG_WRITE | PG_USER;
        }

        vaddr += AMD64_PAGE_SIZE;
        if (phys != PG_ALLOCATEME)
            phys += AMD64_PAGE_SIZE;
    }
}

/* =========================================================================
 * pg_info — return the current PML4 physical address and virtual pointer
 * ========================================================================= */

void pg_info(reg_t *pagedir_ph, u64_t **pagedir_v)
{
    *pagedir_ph = vir2phys(pml4);
    *pagedir_v  = pml4;
}

/* =========================================================================
 * phys_bytes helpers
 * ========================================================================= */

phys_bytes pg_roundup(phys_bytes b)
{
    phys_bytes o = b % AMD64_PAGE_SIZE;
    return o ? b + (AMD64_PAGE_SIZE - o) : b;
}

/* =========================================================================
 * vm_enable_paging — enable PAE + paging in long mode
 * (called during pre-paging bootstrap; in normal boot head.S does this,
 *  but this function can re-apply CR4 flags after prot_init()).
 * ========================================================================= */

void vm_enable_paging(void)
{
    reg_t cr4;

    cr4 = read_cr4();
    cr4 |= CR4_PAE;
    cr4 |= CR4_PGE;     /* global pages */
    write_cr4(cr4);

    /*
     * CR0.PG and CR0.WP are already set by head.S before entering long mode.
     * Re-issuing write_cr0 with those bits while paging is already on causes
     * KVM to re-validate the page tables and fault.  Skip the CR0 write here;
     * prot_init() will call vm_enable_paging() again after paged page tables
     * are loaded, at which point CR0 is already correct.
     */
}

/* =========================================================================
 * Memory-map management (ported from i386, but with 64-bit address limit)
 * ========================================================================= */

void print_memmap(kinfo_t *cbi)
{
    int m;
    assert(cbi->mmap_size < MAXMEMMAP);
    for (m = 0; m < cbi->mmap_size; m++) {
        phys_bytes addr = cbi->memmap[m].mm_base_addr;
        phys_bytes end  = addr + cbi->memmap[m].mm_length;
        printf("%016lx-%016lx ", (unsigned long)addr, (unsigned long)end);
    }
    printf("\nsize %08lx\n", (unsigned long)cbi->mmap_size);
}

void cut_memmap(kinfo_t *cbi, phys_bytes start, phys_bytes end)
{
    int m;
    phys_bytes o;

    if ((o = start % AMD64_PAGE_SIZE)) start -= o;
    if ((o = end   % AMD64_PAGE_SIZE)) end   += AMD64_PAGE_SIZE - o;

    assert(kernel_may_alloc);

    for (m = 0; m < cbi->mmap_size; m++) {
        phys_bytes substart = start, subend = end;
        phys_bytes memaddr = cbi->memmap[m].mm_base_addr;
        phys_bytes memend  = memaddr + cbi->memmap[m].mm_length;

        if (substart < memaddr) substart = memaddr;
        if (subend   > memend ) subend   = memend;
        if (substart >= subend) continue;

        cbi->memmap[m].mm_base_addr = cbi->memmap[m].mm_length = 0;
        if (substart > memaddr)
            add_memmap(cbi, memaddr, substart - memaddr);
        if (subend < memend)
            add_memmap(cbi, subend, memend - subend);
    }
}

phys_bytes alloc_lowest(kinfo_t *cbi, phys_bytes len)
{
    int m;
    phys_bytes lowest = (phys_bytes)-1;

    assert(len > 0);
    len = roundup(len, AMD64_PAGE_SIZE);
    assert(kernel_may_alloc);

    for (m = 0; m < cbi->mmap_size; m++) {
        if (cbi->memmap[m].mm_length < len) continue;
        if (cbi->memmap[m].mm_base_addr < lowest)
            lowest = cbi->memmap[m].mm_base_addr;
    }
    assert(lowest != (phys_bytes)-1);
    cut_memmap(cbi, lowest, len);
    cbi->kernel_allocated_bytes_dynamic += len;
    return lowest;
}

void add_memmap(kinfo_t *cbi, u64_t addr, u64_t len)
{
    int m;

    assert(cbi->mmap_size < MAXMEMMAP);
    if (len == 0) return;

    addr = roundup(addr, AMD64_PAGE_SIZE);
    len  = rounddown(len, AMD64_PAGE_SIZE);
    if (len == 0) return;

    assert(kernel_may_alloc);

    for (m = 0; m < MAXMEMMAP; m++) {
        phys_bytes highmark;
        if (cbi->memmap[m].mm_length) continue;
        cbi->memmap[m].mm_base_addr = addr;
        cbi->memmap[m].mm_length    = len;
        cbi->memmap[m].mm_type      = MULTIBOOT_MEMORY_AVAILABLE;
        if (m >= cbi->mmap_size)
            cbi->mmap_size = m + 1;
        highmark = addr + len;
        if (highmark > cbi->mem_high_phys)
            cbi->mem_high_phys = highmark;
        return;
    }

    panic("no available memmap slot");
}

/*
 * pg_alloc_page — allocate one physical page from the boot memory map.
 * Used exclusively during early boot before the VM server is running.
 */
phys_bytes pg_alloc_page(kinfo_t *cbi)
{
    int m;
    multiboot_memory_map_t *mmap;

    assert(kernel_may_alloc);

    for (m = cbi->mmap_size - 1; m >= 0; m--) {
        mmap = &cbi->memmap[m];
        if (!mmap->mm_length) continue;
        assert(!(mmap->mm_length    % AMD64_PAGE_SIZE));
        assert(!(mmap->mm_base_addr % AMD64_PAGE_SIZE));

        mmap->mm_length -= AMD64_PAGE_SIZE;
        cbi->kernel_allocated_bytes_dynamic += AMD64_PAGE_SIZE;
        return mmap->mm_base_addr + mmap->mm_length;
    }

    panic("can't find free memory");
}
