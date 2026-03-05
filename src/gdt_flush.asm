[global gdt_flush]
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10      ; Kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far jump to kernel code selector (0x08)
    ; We use push/retf to perform a RELOCATABLE far jump in 32-bit mode
    push 0x08
    push .flush
    retf
.flush:
    ret

[global tss_flush]
tss_flush:
    mov ax, 0x28      ; TSS selector (RPL 0)
    ltr ax
    ret
