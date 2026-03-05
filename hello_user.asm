; hello_user.asm - A simple user-mode program for PUNIX
[bits 32]
[org 0x00400000]    ; This matches user_virt_base in load_user_program

_start:
    ; syscall SYS_PRINT (15)
    mov eax, 15
    mov ebx, msg
    int 0x80

    ; syscall SYS_EXIT (11)
    mov eax, 11
    mov ebx, 0      ; status 0
    int 0x80

    ; Fallback in case syscall fails
.halt:
    jmp .halt

msg db "!!! HELLO FROM EXTERNAL USER MODE !!!", 0x0A, 0
