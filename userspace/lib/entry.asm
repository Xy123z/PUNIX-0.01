[BITS 32]
[GLOBAL _start]
[EXTERN main]
[EXTERN sys_exit]

_start:
    ; Provide arguments if needed (argc, argv)
    call main
    
    ; Exit with return value
    push eax
    call sys_exit

    ; Should not reach here
    hlt
