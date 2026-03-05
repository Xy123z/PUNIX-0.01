[global enter_user_mode]

; void enter_user_mode(uint32_t target_eip, uint32_t target_esp)
enter_user_mode:
    ; Parameters: [esp+4] = EIP, [esp+8] = ESP
    
    ; Interrupts are already enabled by kernel_main (sti at line 75 of kernel.c)
    ; Do NOT disable them here, as that prevents keyboard IRQ from working
    mov ebx, [esp + 4]    ; target_eip
    mov eax, [esp + 8]    ; target_esp

    ; Setup segment registers for user mode
    ; Selector 0x20 is User Data (base 0, limit 4GB, RPL 3)
    ; 0x20 | 3 = 0x23
    mov cx, 0x23
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    ; Prepare the stack for IRET
    ; IRET expects: [SS, ESP, EFLAGS, CS, EIP]
    
    push 0x23             ; SS (User Data)
    push eax              ; ESP (User Stack)
    pushf                 ; EFLAGS
    
    ; Ensure IF (interrupt flag) is set in the pushed EFLAGS so interrupts 
    ; are enabled once we reach user mode.
    pop eax
    or eax, 0x200
    push eax

    push 0x1B             ; CS (User Code: 0x18 | 3)
    push ebx              ; EIP (Target function address)

    ; Clear all general purpose registers to prevent leakage
    ; and avoid faults if the program starts with garbage/zeros
    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    ; We keep ebx for a moment if we need it, but let's clear it too
    ; except that the push ebx already happened.
    ; Actually, we can clear ebx now since it's already on the stack.
    xor ebx, ebx

    iret
