/* Exyde x86_64 kernel linker script.
 *
 * Layout is deliberately simple for the first milestone:
 *   - kernel is linked at 1 MiB
 *   - .multiboot2 must be the first section in the file
 *   - .boot (page tables, stacks) is loaded but not zeroed
 *   - .bss is zeroed by kmain() before anything else runs
 */

ENTRY(_start)

SECTIONS
{
    . = 1M;

    __kernel_start = .;

    .multiboot2 : ALIGN(8)
    {
        KEEP(*(.multiboot2))
    }

    .text : ALIGN(4K)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    /* Kernel exception table: { u64 fault_rip; u64 fixup_rip; } pairs.
     * Consumed by extable_lookup() in core/extable.c. */
    .extable : ALIGN(16)
    {
        __extable_start = .;
        KEEP(*(.extable))
        __extable_end = .;
    }

    .data : ALIGN(4K)
    {
        *(.data .data.*)
    }

    .boot (NOLOAD) : ALIGN(4K)
    {
        __boot_start = .;
        *(.boot.data)
        *(.boot.bss)
        __boot_end = .;
    }

    .bss (NOLOAD) : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
    }
}
