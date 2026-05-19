/*
 * head.S - amd64 kernel entry point.
 *
 * The Multiboot-compliant bootloader starts us in 32-bit protected mode with
 * paging disabled.  From here we must:
 *
 *   1. Set up minimal temporary 4-level page tables (identity-map first 4 GB).
 *   2. Enable PAE (CR4.PAE) and the Long Mode Enable bit (EFER.LME).
 *   3. Enable paging (CR0.PG) to activate Long Mode.
 *   4. Far-jump into a 64-bit code segment to flush the instruction pipeline.
 *   5. Set up a real 64-bit stack and call pre_init() / kmain().
 *
 * All code before the far jump is compiled as 32-bit (.code32) because the
 * CPU is still in 32-bit compatibility mode at that point.
 */

#include "kernel/kernel.h"

#include <machine/vm.h>
#include <machine/asm.h>
#include <machine/multiboot.h>
#include <minix/config.h>
#include <minix/const.h>
#include "include/archconst.h"
#include "include/arch_proto.h"

/* Temporary GDT selector values used during bootstrap only. */
#define BOOT_CS32   0x08    /* 32-bit code segment */
#define BOOT_CS64   0x10    /* 64-bit code segment */
#define BOOT_DS     0x18    /* data segment (used for both modes) */

/*===========================================================================*
 *  Multiboot header                                                          *
 *===========================================================================*/
.section ".multiboot", "a"
.align 8
multiboot_magic_val:
    .long   MULTIBOOT_HEADER_MAGIC
multiboot_flags_val:
    .long   (MULTIBOOT_HEADER_WANT_MEMORY | MULTIBOOT_HEADER_MODS_ALIGNED)
multiboot_checksum_val:
    .long   -(MULTIBOOT_HEADER_MAGIC + \
              (MULTIBOOT_HEADER_WANT_MEMORY | MULTIBOOT_HEADER_MODS_ALIGNED))
    /* unused fields */
    .long 0, 0, 0, 0, 0
/* Video mode request */
    .long MULTIBOOT_VIDEO_MODE_EGA
    .long MULTIBOOT_CONSOLE_COLS
    .long MULTIBOOT_CONSOLE_LINES
    .long 0

/*===========================================================================*
 *  32-bit bootstrap code                                                     *
 *===========================================================================*/
.code32
.text

.global MINIX
MINIX:
    jmp     bootstrap32

bootstrap32:
    /*
     * Bootloader gives us:
     *   EAX = MULTIBOOT_BOOTLOADER_MAGIC  (0x2BADB002)
     *   EBX = physical address of multiboot_info_t
     *
     * Carry magic in EBX and info ptr in ESI through the page-table setup.
     * EDI and ESI would seem natural, but the page-table zeroing loops use
     * EDI as the rep-stosl destination pointer, clobbering it immediately.
     * EBX is not touched anywhere between here and the 64-bit call to
     * pre_init(), so it is the safe register for the magic value.
     */
    mov     %ebx, %esi          /* info ptr → ESI (not clobbered by PT setup) */
    mov     %eax, %ebx          /* magic  → EBX (not clobbered by PT setup) */

    /* Set up a small temporary stack. */
    mov     $(_C_LABEL(boot32_stktop)), %esp
    xor     %ebp, %ebp

    /* Clear EFLAGS to a known state. */
    pushl   $0
    popfl

    /* ------------------------------------------------------------------
     * Step 1: Build temporary 4-level page tables.
     *
     * We identity-map the first 4 GB using 1 PML4 entry -> 1 PDPT entry
     * -> 512 PD entries with 2 MB pages (PS bit set).  This keeps the
     * bootstrap page tables very small (one page each for PML4, PDPT, PD).
     * ------------------------------------------------------------------
     */

    /* Zero PML4 table. */
    mov     $(_C_LABEL(boot_pml4)), %edi
    xor     %eax, %eax
    mov     $1024, %ecx             /* 4096 bytes / 4 bytes per stosl */
    rep stosl

    /* Zero PDPT. */
    mov     $(_C_LABEL(boot_pdpt)), %edi
    xor     %eax, %eax
    mov     $1024, %ecx
    rep stosl

    /* Zero PD. */
    mov     $(_C_LABEL(boot_pd)), %edi
    xor     %eax, %eax
    mov     $1024, %ecx
    rep stosl

    /* PML4[0] -> PDPT (present + writable) */
    mov     $(_C_LABEL(boot_pdpt)), %eax
    or      $0x03, %eax         /* present | writable */
    mov     %eax, (_C_LABEL(boot_pml4))

    /* PDPT[0] -> PD (present + writable) */
    mov     $(_C_LABEL(boot_pd)), %eax
    or      $0x03, %eax
    mov     %eax, (_C_LABEL(boot_pdpt))

    /*
     * PD[0..511]: identity-map 512 * 2 MB = 1 GB.
     * Each entry: phys = i * 2MB, flags = present | writable | PS (2MB page).
     */
    mov     $(_C_LABEL(boot_pd)), %edi
    xor     %ecx, %ecx
1:
    mov     %ecx, %eax
    shl     $21, %eax           /* i * 2 MB */
    or      $0x83, %eax         /* present | writable | PS */
    mov     %eax, (%edi)
    add     $8, %edi
    inc     %ecx
    cmp     $512, %ecx
    jl      1b

    /* ------------------------------------------------------------------
     * Step 2: Load CR3 with the PML4 physical address.
     * ------------------------------------------------------------------
     */
    mov     $(_C_LABEL(boot_pml4)), %eax
    mov     %eax, %cr3

    /* ------------------------------------------------------------------
     * Step 3: Enable PAE (CR4.PAE = bit 5).
     * ------------------------------------------------------------------
     */
    mov     %cr4, %eax
    or      $CR4_PAE, %eax
    mov     %eax, %cr4

    /* ------------------------------------------------------------------
     * Step 4: Set Long Mode Enable (EFER.LME = bit 8) via MSR.
     * ------------------------------------------------------------------
     */
    mov     $AMD_MSR_EFER, %ecx
    rdmsr
    or      $AMD_EFER_LME, %eax
    wrmsr

    /* ------------------------------------------------------------------
     * Step 5: Enable paging (CR0.PG = bit 31).
     * This atomically activates Long Mode (LMA will be set by the CPU).
     * ------------------------------------------------------------------
     */
    mov     %cr0, %eax
    or      $(CR0_PG | CR0_WP), %eax
    mov     %eax, %cr0

    /* ------------------------------------------------------------------
     * Step 6: Far jump to flush the pipeline and enter 64-bit mode.
     * We use the temporary GDT defined below.
     * ------------------------------------------------------------------
     */
    lgdt    boot_gdtr
    ljmp    $BOOT_CS64, $(_C_LABEL(longmode_start))

/*===========================================================================*
 *  64-bit kernel startup                                                     *
 *===========================================================================*/
.code64
.align 16

ENTRY(longmode_start)
    /* Load 64-bit data segments. */
    mov     $BOOT_DS, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    xor     %ax, %ax
    mov     %ax, %fs
    mov     %ax, %gs

    /* Initialize COM1 at 115200 8N1 so the kernel's later printf path can
     * write to the serial port (used when cttyline=0 is set in boot args). */
    movw    $0x3FB, %dx; movb $0x80, %al; outb %al, %dx  /* DLAB=1 */
    movw    $0x3F8, %dx; movb $0x01, %al; outb %al, %dx  /* DLL=1 (115200) */
    movw    $0x3F9, %dx; movb $0x00, %al; outb %al, %dx  /* DLM=0 */
    movw    $0x3FB, %dx; movb $0x03, %al; outb %al, %dx  /* 8N1, DLAB=0 */
    movw    $0x3FA, %dx; movb $0xC7, %al; outb %al, %dx  /* FIFO enable */
    movw    $0x3FC, %dx; movb $0x0B, %al; outb %al, %dx  /* RTS+DTR */

    /* Emit "HEAD64\r\n" directly via COM1 to confirm we got here.
     * (Diagnostic to localize early-boot hangs that occur before printf.) */
    movw    $0x3F8, %dx
    movb    $'H', %al; outb %al, %dx
    movb    $'E', %al; outb %al, %dx
    movb    $'A', %al; outb %al, %dx
    movb    $'D', %al; outb %al, %dx
    movb    $'6', %al; outb %al, %dx
    movb    $'4', %al; outb %al, %dx
    movb    $'\r', %al; outb %al, %dx
    movb    $'\n', %al; outb %al, %dx

    /* Set up the initial 64-bit stack. */
    movabs  $(_C_LABEL(k_initial_stktop)), %rsp
    xor     %rbp, %rbp

    /* pre_init(u32_t magic, u32_t ebx): SysV AMD64 ABI — RDI, RSI.
     * magic was carried in EBX/RBX (zero-extended on any 32-bit write);
     * info ptr was carried in ESI/RSI.  Move magic into RDI now.
     */
    mov     %rbx, %rdi          /* magic (RBX) → first arg (RDI) */
    /* RSI already holds the multiboot info pointer */
    call    _C_LABEL(pre_init)

    /* pre_init() returns the kinfo_t * boot pointer in RAX.
     * Switch to the high kernel stack and jump to kmain(). */
    movabs  $(_C_LABEL(k_initial_stktop)), %rsp
    xor     %rbp, %rbp
    push    $0              /* terminate stack traces */

    mov     %rax, %rdi      /* pass boot info to kmain (first arg) */

    /* Save RDI across COM1 emit. */
    push    %rdi
    /* Emit "KMAIN\r\n" via COM1 — confirms pre_init returned cleanly. */
    movw    $0x3F8, %dx
    movb    $'K', %al; outb %al, %dx
    movb    $'M', %al; outb %al, %dx
    movb    $'A', %al; outb %al, %dx
    movb    $'I', %al; outb %al, %dx
    movb    $'N', %al; outb %al, %dx
    movb    $'\r', %al; outb %al, %dx
    movb    $'\n', %al; outb %al, %dx
    pop     %rdi

    call    _C_LABEL(kmain)

    /* Not reached. */
hang64:
    hlt
    jmp     hang64

/*===========================================================================*
 *  Bootstrap GDT (minimal; replaced by prot_init() later)                   *
 *===========================================================================*/
.align 8
boot_gdt:
    /* 0x00: null descriptor */
    .quad 0

    /* 0x08: 32-bit code segment (needed for the pre-jump state) */
    .word 0xFFFF, 0x0000
    .byte 0x00, 0x9A, 0xCF, 0x00   /* present, DPL=0, code, 32-bit, 4GB */

    /* 0x10: 64-bit code segment */
    .word 0xFFFF, 0x0000
    .byte 0x00, 0x9A, 0xAF, 0x00   /* present, DPL=0, code, L=1, G=1 */

    /* 0x18: data segment (shared by 32-bit and 64-bit mode) */
    .word 0xFFFF, 0x0000
    .byte 0x00, 0x92, 0xCF, 0x00   /* present, DPL=0, data, 4GB */

boot_gdtr:
    .word   (. - boot_gdt - 1)     /* limit */
    .long   boot_gdt               /* base (32-bit physical during bootstrap) */

/*===========================================================================*
 *  BSS / data                                                                *
 *===========================================================================*/
.bss

/* Temporary 32-bit bootstrap stack (4 KB). */
boot32_stack:
    .space  4096
boot32_stktop:

/* Temporary page tables — must be 4 KB aligned. */
.align 4096
LABEL(boot_pml4)
    .space  4096

.align 4096
LABEL(boot_pdpt)
    .space  4096

.align 4096
LABEL(boot_pd)
    .space  4096

/* Initial 64-bit kernel stack (one page). */
.align 4096
k_initial_stack:
    .space  K_STACK_SIZE
LABEL(k_initial_stktop)
LABEL(k_boot_stktop)    /* alias used by protect.c tss_init(0, ...) */

/* Per-CPU kernel stacks (two pages each: one data page + one guard page). */
.align K_STACK_SIZE
LABEL(k_stacks_start)
    .space  2 * (K_STACK_SIZE * CONFIG_MAX_CPUS)
LABEL(k_stacks_end)
