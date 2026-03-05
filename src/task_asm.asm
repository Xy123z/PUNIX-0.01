[bits 32]

section .text

; void switch_to(uint32_t* old_esp, uint32_t new_esp)
global switch_to
switch_to:
    push ebp
    push ebx
    push esi
    push edi
    
    mov eax, [esp + 20]     ; address of old_esp
    mov [eax], esp          ; save current esp
    
    mov eax, [esp + 24]     ; new_esp
    mov esp, eax            ; switch to new esp
    
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; Used as the entry point for new tasks
global task_return
task_return:
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8 ; skip error code and interrupt number (if any)
    iret
