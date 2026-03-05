[bits 32]

section .text
global _start
extern main
extern sys_exit

_start:
    ; For now, we don't handle argc/argv significantly
    ; just call main()
    call main
    
    ; Exit with main's return value
    push eax
    call sys_exit
    
    ; Should never reach here
    hlt
