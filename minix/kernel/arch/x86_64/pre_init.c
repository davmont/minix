/*
 * pre_init.c - amd64 early kernel initialisation (runs before paging).
 *
 * Adapted from arch/i386/pre_init.c.  The main differences:
 *
 *   - pre_init() receives (u32_t magic, u32_t ebx) from head.S.  On amd64
 *     the calling convention places these in RDI / RSI by the time we arrive
 *     in longmode_start, but head.S already put them there via the 32-bit EDI
 *     and ESI registers (zero-extended to 64 bits by the mode switch).
 *
 *   - No 4 GB memory ceiling: add_memmap() in pg_utils.c accepts u64_t
 *     addresses, so we pass the full MMAP entries unchanged.
 *
 *   - Page-size references use AMD64_PAGE_SIZE (4096) directly, since
 *     the I386_PAGE_SIZE constant is not defined for the amd64 target.
 */

#define UNPAGED 1   /* selects the unpaged-safe kmain() prototype */

#include <assert.h>
#include <stdlib.h>
#include <minix/minlib.h>
#include <minix/board.h>
#include <sys/reboot.h>
#include <machine/partition.h>
#include "string.h"
#include "serial.h"
#include "glo.h"

#if USE_SYSDEBUG
#define MULTIBOOT_VERBOSE 1
#endif

/* To-be-built kinfo struct and diagnostics buffer. */
kinfo_t          kinfo;
struct kmessages kmessages;

/*
 * During the unpaged phase there is a 1:1 mapping, so virtual == physical.
 * pg_utils.c calls vir2phys() only after paging is on (where protect.c
 * provides the real version); this stub covers the bootstrap window.
 */
phys_bytes vir2phys(void *addr) { return (phys_bytes)(uintptr_t)addr; }

/* video_mem is used by direct_tty_utils.c for early console output. */
char *video_mem = (char *)MULTIBOOT_VIDEO_BUFFER;

#define ITOA_BUFFER_SIZE 20

/* Permit early kernel memory allocation. */
int kernel_may_alloc = 1;

/* AMD64 page size (4 KB). */
#ifndef AMD64_PAGE_SIZE
#define AMD64_PAGE_SIZE 4096UL
#endif


/*===========================================================================*
 *  mb_set_param                                                              *
 *===========================================================================*/
static int mb_set_param(char *bigbuf, char *name, char *value, kinfo_t *cbi)
{
    char *p = bigbuf;
    char *bufend = bigbuf + MULTIBOOT_PARAM_BUF_SIZE;
    char *q;
    int namelen  = strlen(name);
    int valuelen = strlen(value);

    if (!strcmp(name, SERVARNAME))    { cbi->do_serial_debug   = 1; }
    if (!strcmp(name, SERBAUDVARNAME)){ cbi->serial_debug_baud = atoi(value); }

    /* Delete existing entry with the same name. */
    while (*p) {
        if (strncmp(p, name, namelen) == 0 && p[namelen] == '=') {
            q = p;
            while (*q) q++;
            for (q++; q < bufend; q++, p++) *p = *q;
            break;
        }
        while (*p++) ;
        p++;
    }

    for (p = bigbuf; p < bufend && (*p || *(p + 1)); p++) ;
    if (p > bigbuf) p++;

    if (p + namelen + valuelen + 3 > bufend) return -1;

    strcpy(p, name);
    p[namelen] = '=';
    strcpy(p + namelen + 1, value);
    p[namelen + valuelen + 1] = 0;
    p[namelen + valuelen + 2] = 0;
    return 0;
}

/*===========================================================================*
 *  overlaps                                                                  *
 *===========================================================================*/
int overlaps(multiboot_module_t *mod, int n, int cmp_mod)
{
    multiboot_module_t *cmp = &mod[cmp_mod];
    int m;

#define INRANGE(mod, v) ((v) >= (mod)->mod_start && (v) < (mod)->mod_end)
#define OVERLAP(m1, m2) (INRANGE(m1,(m2)->mod_start) || INRANGE(m1,(m2)->mod_end-1))

    for (m = 0; m < n; m++) {
        if (m == cmp_mod) continue;
        if (OVERLAP(&mod[m], cmp)) return 1;
    }
    return 0;
}

/*===========================================================================*
 *  get_parameters                                                            *
 *===========================================================================*/
void get_parameters(u32_t ebx, kinfo_t *cbi)
{
    multiboot_memory_map_t *mmap;
    multiboot_info_t *mbi = &cbi->mbi;
    int var_i, value_i, m, k;
    char *p;
    extern char _kern_phys_base, _kern_vir_base, _kern_size,
                _kern_unpaged_start, _kern_unpaged_end;
    phys_bytes kernbase = (phys_bytes)(uintptr_t)&_kern_phys_base;
    phys_bytes kernsize = (phys_bytes)(uintptr_t)&_kern_size;
#define BUF 1024
    static char cmdline[BUF];

    memcpy((void *)mbi, (void *)(uintptr_t)ebx, sizeof(*mbi));

    cbi->mem_high_phys   = 0;
    cbi->user_sp         = (vir_bytes)(uintptr_t)&_kern_vir_base;
    cbi->vir_kern_start  = (vir_bytes)(uintptr_t)&_kern_vir_base;
    cbi->bootstrap_start = (vir_bytes)(uintptr_t)&_kern_unpaged_start;
    cbi->bootstrap_len   = (vir_bytes)(uintptr_t)&_kern_unpaged_end
                         - cbi->bootstrap_start;
    cbi->kmess           = &kmess;

    cbi->do_serial_debug   = 0;
    cbi->serial_debug_baud = 115200;

    /* Parse boot command line. */
    if (mbi->mi_flags & MULTIBOOT_INFO_HAS_CMDLINE) {
        static char var[BUF];
        static char value[BUF];

        memcpy(cmdline, (void *)(uintptr_t)mbi->mi_cmdline, BUF);

        /* Dump raw cmdline bytes via COM1 so we can see what we got.
         * Unconditional because this is unpaged code — `verboseboot`
         * lives in paged BSS which isn't mapped at unpaged-execution
         * time, so a BOOT_VERBOSE gate here would page-fault.  Output
         * is a single short line at boot. */
        {
            int dump_i;
            #define PI_COM1(c) __asm__ __volatile__("outb %0, %1" : : "a"((char)(c)), "Nd"((unsigned short)0x3F8))
            PI_COM1('C'); PI_COM1('L'); PI_COM1('=');
            for (dump_i = 0; dump_i < 200 && cmdline[dump_i]; dump_i++) {
                PI_COM1(cmdline[dump_i]);
            }
            PI_COM1('|'); PI_COM1('E'); PI_COM1('O'); PI_COM1('L');
            PI_COM1('\r'); PI_COM1('\n');
            #undef PI_COM1
        }

        p = cmdline;
        while (*p) {
            var_i = value_i = 0;
            while (*p == ' ') p++;
            if (!*p) break;
            while (*p && *p != '=' && *p != ' ' && var_i < BUF - 1)
                var[var_i++] = *p++;
            var[var_i] = 0;
            if (*p++ != '=') continue;
            while (*p && *p != ' ' && value_i < BUF - 1)
                value[value_i++] = *p++;
            value[value_i] = 0;
            mb_set_param(cbi->param_buf, var, value, cbi);
        }
    }

    mb_set_param(cbi->param_buf, ARCHVARNAME,
                 (char *)get_board_arch_name(BOARD_ID_INTEL_AMD64), cbi);
    mb_set_param(cbi->param_buf, BOARDVARNAME,
                 (char *)get_board_name(BOARD_ID_INTEL_AMD64), cbi);

    cbi->user_sp  = USR_STACKTOP;
    cbi->user_end = USR_DATATOP;

    kinfo.kernel_allocated_bytes  = (phys_bytes)(uintptr_t)&_kern_size;
    kinfo.kernel_allocated_bytes -= cbi->bootstrap_len;

    assert(!(cbi->bootstrap_start % AMD64_PAGE_SIZE));
    cbi->bootstrap_len = rounddown(cbi->bootstrap_len, AMD64_PAGE_SIZE);

    assert(mbi->mi_flags & MULTIBOOT_INFO_HAS_MODS);
    assert(mbi->mi_mods_count < MULTIBOOT_MAX_MODS);
    assert(mbi->mi_mods_count > 0);
    memcpy(&cbi->module_list, (void *)(uintptr_t)mbi->mi_mods_addr,
           mbi->mi_mods_count * sizeof(multiboot_module_t));
    memset(cbi->memmap, 0, sizeof(cbi->memmap));

    if (mbi->mi_flags & MULTIBOOT_INFO_HAS_MMAP) {
        cbi->mmap_size = 0;
        for (mmap = (multiboot_memory_map_t *)(uintptr_t)mbi->mmap_addr;
             (unsigned long)mmap < mbi->mmap_addr + mbi->mmap_length;
             mmap = (multiboot_memory_map_t *)
                    ((unsigned long)mmap + mmap->mm_size + sizeof(mmap->mm_size))) {
            if (mmap->mm_type != MULTIBOOT_MEMORY_AVAILABLE) continue;
            /* Pass full 64-bit base + length; pg_utils.c has no 4 GB cap. */
            add_memmap(cbi, mmap->mm_base_addr, mmap->mm_length);
        }
    } else {
        assert(mbi->mi_flags & MULTIBOOT_INFO_HAS_MEMORY);
        add_memmap(cbi, 0, (u64_t)mbi->mi_mem_lower * 1024);
        add_memmap(cbi, 0x100000, (u64_t)mbi->mi_mem_upper * 1024);
    }

    /* Check that kernel and modules don't overlap each other. */
    k = mbi->mi_mods_count;
    assert(k < MULTIBOOT_MAX_MODS);
    cbi->module_list[k].mod_start = kernbase;
    cbi->module_list[k].mod_end   = kernbase + kernsize;
    cbi->mods_with_kernel         = mbi->mi_mods_count + 1;
    cbi->kern_mod                 = k;

    for (m = 0; m < cbi->mods_with_kernel; m++) {
        if (overlaps(cbi->module_list, cbi->mods_with_kernel, m))
            panic("overlapping boot modules/kernel");
        cut_memmap(cbi,
                   cbi->module_list[m].mod_start,
                   cbi->module_list[m].mod_end);
    }
}

/*===========================================================================*
 *  pre_init                                                                  *
 *===========================================================================*/
kinfo_t *pre_init(u32_t magic, u32_t ebx)
{
    assert(magic == MULTIBOOT_INFO_MAGIC);

    get_parameters(ebx, &kinfo);

    pg_clear();
    pg_identity(&kinfo);
    kinfo.freepde_start = pg_mapkernel();
    pg_load();
    vm_enable_paging();

    return &kinfo;
}

/*===========================================================================*
 *  Stubs required by the unpaged link unit                                   *
 *===========================================================================*/
void send_diag_sig(void)     { }
void minix_shutdown(int how) { arch_shutdown(how); }
void busy_delay_ms(int x)    { (void)x; }
int  raise(int sig)          { panic("raise(%d)\n", sig); }
