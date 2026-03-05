ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    . = 0x80000000;

    .text :
    {
        *(.text.entry)
        *(.text)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}
