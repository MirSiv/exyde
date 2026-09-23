OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x0000008000000000;

    .text : ALIGN(16)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(16)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(16)
    {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
        *(.note.gnu.*)
    }
}
