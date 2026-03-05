; src/boot_entry.asm - Guarantees the kernel entry point address
[bits 32]
[global _start]
[extern kernel_main]

_start:
    ; --- VGA 'TEST' Marker ---
    ; Just to visually confirm we reached the entry point
    mov eax, 0xB8000
    mov byte [eax], 'T'
    mov byte [eax+1], 0x0F
    mov byte [eax+2], 'E'
    mov byte [eax+3], 0x0F
    mov byte [eax+4], 'S'
    mov byte [eax+5], 0x0F
    mov byte [eax+6], 'T'
    mov byte [eax+7], 0x0F

    ; Jump to the C kernel entry point
    call kernel_main

    ; Should never return, but if it does, halt
.halt:
    cli
    hlt
    jmp .halt
