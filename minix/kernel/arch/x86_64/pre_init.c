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
#include "multiboot2.h"

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
 *  mb2_to_mb1                                                                *
 *                                                                            *
 *  Translate a Multiboot2 information block (handed to us by GRUB under      *
 *  UEFI) into the Multiboot1 multiboot_info_t that get_parameters() already  *
 *  knows how to consume, and capture the two things Multiboot1 cannot carry: *
 *  the ACPI RSDP and the EFI linear framebuffer.  Returns the physical       *
 *  address of the synthesised multiboot_info_t.                              *
 *                                                                            *
 *  All synthesised structures are static in this (unpaged) translation unit, *
 *  so during pre_init's 1:1 window their addresses are valid physical        *
 *  addresses that get_parameters() can dereference.                          *
 *===========================================================================*/
#define MB2_MAX_MMAP 128
#ifndef BUF
#define BUF 1024
#endif
static multiboot_info_t       mb2_mbi;
static multiboot_module_t     mb2_modlist[MULTIBOOT_MAX_MODS];
static multiboot_memory_map_t mb2_mmap[MB2_MAX_MMAP];
static char                   mb2_cmdline[BUF];

static u32_t mb2_to_mb1(u32_t info_phys, kinfo_t *cbi)
{
    struct multiboot2_info *info = (struct multiboot2_info *)(uintptr_t)info_phys;
    u8_t *p   = (u8_t *)info + 8;            /* first tag follows the 8-byte hdr */
    u8_t *end = (u8_t *)info + info->total_size;
    int nmod = 0, nmmap = 0;

    memset(&mb2_mbi, 0, sizeof(mb2_mbi));
    cbi->acpi_rsdp = 0;
    cbi->fb_addr   = 0;

    while (p + sizeof(struct multiboot2_tag) <= end) {
        struct multiboot2_tag *tag = (struct multiboot2_tag *)p;

        if (tag->type == MULTIBOOT2_TAG_END)
            break;

        switch (tag->type) {
        case MULTIBOOT2_TAG_CMDLINE: {
            struct multiboot2_tag_string *t = (struct multiboot2_tag_string *)tag;
            int i;
            for (i = 0; i < BUF - 1 && t->string[i]; i++)
                mb2_cmdline[i] = t->string[i];
            mb2_cmdline[i] = 0;
            mb2_mbi.mi_cmdline = (u32_t)(uintptr_t)mb2_cmdline;
            mb2_mbi.mi_flags  |= MULTIBOOT_INFO_HAS_CMDLINE;
            break;
        }
        case MULTIBOOT2_TAG_MODULE: {
            struct multiboot2_tag_module *t = (struct multiboot2_tag_module *)tag;
            if (nmod < MULTIBOOT_MAX_MODS) {
                mb2_modlist[nmod].mmo_start    = t->mod_start;
                mb2_modlist[nmod].mmo_end      = t->mod_end;
                mb2_modlist[nmod].mmo_string   = 0;
                mb2_modlist[nmod].mmo_reserved = 0;
                nmod++;
            }
            break;
        }
        case MULTIBOOT2_TAG_MMAP: {
            struct multiboot2_tag_mmap *t = (struct multiboot2_tag_mmap *)tag;
            u8_t *e    = (u8_t *)t->entries;
            u8_t *mend = (u8_t *)tag + tag->size;
            if (t->entry_size == 0) break;
            for (; e + t->entry_size <= mend && nmmap < MB2_MAX_MMAP;
                 e += t->entry_size) {
                struct multiboot2_mmap_entry *me =
                    (struct multiboot2_mmap_entry *)e;
                mb2_mmap[nmmap].mm_size      = 20;  /* mb1: size excl. mm_size */
                mb2_mmap[nmmap].mm_base_addr = me->addr;
                mb2_mmap[nmmap].mm_length    = me->len;
                mb2_mmap[nmmap].mm_type      = me->type;
                nmmap++;
            }
            break;
        }
        case MULTIBOOT2_TAG_FRAMEBUFFER: {
            struct multiboot2_tag_framebuffer *t =
                (struct multiboot2_tag_framebuffer *)tag;
            cbi->fb_addr   = (phys_bytes)t->framebuffer_addr;
            cbi->fb_pitch  = t->framebuffer_pitch;
            cbi->fb_width  = t->framebuffer_width;
            cbi->fb_height = t->framebuffer_height;
            cbi->fb_bpp    = t->framebuffer_bpp;
            cbi->fb_type   = t->framebuffer_type;
            break;
        }
        case MULTIBOOT2_TAG_ACPI_OLD:
        case MULTIBOOT2_TAG_ACPI_NEW: {
            struct multiboot2_tag_acpi *t = (struct multiboot2_tag_acpi *)tag;
            /* The tag carries a verbatim copy of the RSDP; its physical
             * address is what the kernel ACPI code will read.  Prefer the
             * v1 ("old") tag if both are present, since the kernel uses the
             * 32-bit rsdt_addr in the first 20 bytes. */
            if (cbi->acpi_rsdp == 0 || tag->type == MULTIBOOT2_TAG_ACPI_OLD)
                cbi->acpi_rsdp = (phys_bytes)(uintptr_t)t->rsdp;
            break;
        }
        default:
            break;
        }

        /* Tags are padded up to an 8-byte boundary. */
        p += (tag->size + 7) & ~(u32_t)7;
    }

    if (nmmap > 0) {
        mb2_mbi.mi_mmap_addr   = (u32_t)(uintptr_t)mb2_mmap;
        mb2_mbi.mi_mmap_length = nmmap * sizeof(multiboot_memory_map_t);
        mb2_mbi.mi_flags      |= MULTIBOOT_INFO_HAS_MMAP;
    }
    mb2_mbi.mi_mods_count = nmod;
    mb2_mbi.mi_mods_addr  = (u32_t)(uintptr_t)mb2_modlist;
    mb2_mbi.mi_flags     |= MULTIBOOT_INFO_HAS_MODS;

    return (u32_t)(uintptr_t)&mb2_mbi;
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
    /*
     * Two entry protocols are supported.  A Multiboot1 loader (the BIOS /
     * NetBSD boot path) leaves 0x2BADB002 in EAX and a multiboot_info_t
     * pointer in EBX.  A Multiboot2 loader (GRUB under UEFI) leaves
     * 0x36D76289 in EAX and a tag-based information block in EBX; translate
     * it into the Multiboot1 form get_parameters() expects, also capturing
     * the ACPI RSDP and EFI framebuffer that Multiboot1 cannot carry.
     */
    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        ebx   = mb2_to_mb1(ebx, &kinfo);
        magic = MULTIBOOT_INFO_MAGIC;
    }

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
