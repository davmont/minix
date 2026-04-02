/*
 * kernel.lds - amd64 kernel linker script.
 *
 * The kernel is compiled for its high virtual address (_kern_vir_base) but
 * physically loaded at _kern_phys_base by the bootloader.  All LMA (load)
 * addresses are therefore shifted by -_kern_offset from the VMA.
 *
 * Memory layout:
 *
 *   [_kern_phys_base .. ]   .unpaged_* sections (identity-mapped, run before
 *                           paging is enabled – head.S, pre_init, pg_utils …)
 *
 *   [_kern_vir_base .. ]    paged sections (.usermapped, .text, .data, .bss)
 *
 * Physical base is placed at 2 MB so the first 2 MB (including the real-mode
 * IVT and BIOS data area) are left untouched and the bootstrap page tables
 * can use a single 2 MB page entry for the kernel itself.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH("i386:x86-64")
ENTRY(__k_unpaged_MINIX)

_kern_phys_base = 0x0000000000200000;   /* phys 2 MB */
_kern_vir_base  = 0xFFFFFFFF80200000;   /* -2 GB + 2 MB (kernel space) */
_kern_offset    = (_kern_vir_base - _kern_phys_base);

__k_unpaged__kern_offset      = _kern_offset;
__k_unpaged__kern_vir_base    = _kern_vir_base;
__k_unpaged__kern_phys_base   = _kern_phys_base;

SECTIONS
{
    /*=================================================================
     * Unpaged region: runs before paging is enabled.
     * Physical == virtual here (identity-mapped by the bootstrap PD).
     *=================================================================*/
    . = _kern_phys_base;
    __k_unpaged__kern_unpaged_start = .;

    .unpaged_text  : { unpaged_*.o(.text .text.*) }
    .unpaged_rodata ALIGN(4096) : { unpaged_*.o(.rodata .rodata.*) }
    .unpaged_data  ALIGN(4096) : { unpaged_*.o(.data) }
    .unpaged_bss   ALIGN(4096) :
    {
        unpaged_*.o(.bss .bss.* COMMON)
        /* Temporary bootstrap page tables (allocated in head.S .bss). */
        *(.boot_pgtables)
    }

    __k_unpaged__kern_unpaged_end = .;

    /*=================================================================
     * Paged region: VMA high, LMA = VMA - _kern_offset.
     *=================================================================*/
    . += _kern_offset;

    /* Usermapped sections (shared read-only with userland). */
    . = ALIGN(4096);
    usermapped_start = .;
    .usermapped_glo : AT(ADDR(.usermapped_glo) - _kern_offset)
    {
        *(.usermapped_glo)
    }
    . = ALIGN(4096);
    usermapped_nonglo_start = .;
    .usermapped : AT(ADDR(.usermapped) - _kern_offset)
    {
        *(.usermapped)
    }
    . = ALIGN(4096);
    usermapped_end = .;

    /* Main kernel text. */
    .text : AT(ADDR(.text) - _kern_offset)
    {
        *(.text .text.*)
    }

    /* Read-only data. */
    .rodata ALIGN(4096) : AT(ADDR(.rodata) - _kern_offset)
    {
        *(.rodata .rodata.*)
    }

    /* Read-write data. */
    .data ALIGN(4096) : AT(ADDR(.data) - _kern_offset)
    {
        *(.data .data.*)
    }

    /* BSS (zero-initialised). */
    . = ALIGN(4096);
    .bss ALIGN(4096) : AT(ADDR(.bss) - _kern_offset)
    {
        *(.bss .bss.* COMMON)

        /* Expose kernel size to C and the unpaged namespace. */
        __k_unpaged__kern_size = . - _kern_vir_base;
        _kern_size = __k_unpaged__kern_size;
    }

    _end = .;
}
