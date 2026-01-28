[bits 32]

section .text

%macro SYSCALL_0 2
global %1
%1:
    mov eax, %2
    int 0x80
    ret
%endmacro

%macro SYSCALL_1 2
global %1
%1:
    push ebx
    mov eax, %2
    mov ebx, [esp + 8]
    int 0x80
    pop ebx
    ret
%endmacro

%macro SYSCALL_2 2
global %1
%1:
    push ebx
    mov eax, %2
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    int 0x80
    pop ebx
    ret
%endmacro

%macro SYSCALL_3 2
global %1
%1:
    push ebx
    mov eax, %2
    mov ebx, [esp + 8]
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    int 0x80
    pop ebx
    ret
%endmacro

%macro SYSCALL_4 2
global %1
%1:
    push ebx
    push esi
    mov eax, %2
    mov ebx, [esp + 12]
    mov ecx, [esp + 16]
    mov edx, [esp + 20]
    mov esi, [esp + 24]
    int 0x80
    pop esi
    pop ebx
    ret
%endmacro

SYSCALL_3 sys_read, 0
SYSCALL_3 sys_write, 1
SYSCALL_2 sys_open, 2
SYSCALL_1 sys_close, 3
SYSCALL_3 sys_getdents, 4
SYSCALL_1 sys_chdir, 5
SYSCALL_2 sys_getcwd, 6
SYSCALL_1 sys_mkdir, 7
SYSCALL_1 sys_rmdir, 8
SYSCALL_1 sys_unlink, 9
SYSCALL_2 sys_stat, 10
SYSCALL_1 sys_exit, 11
SYSCALL_0 sys_getpid, 12
SYSCALL_1 sys_malloc, 13
SYSCALL_1 sys_free, 14
SYSCALL_1 sys_print, 15
SYSCALL_1 sys_create_file, 16
SYSCALL_0 sys_raw_getchar, 17
SYSCALL_1 sys_putchar, 18
SYSCALL_2 sys_print_colored, 19
SYSCALL_0 sys_clear_screen, 20
SYSCALL_3 sys_get_disk_stats, 21
SYSCALL_3 sys_get_cache_stats, 22
SYSCALL_0 sys_sync, 23
SYSCALL_1 sys_chuser, 24
SYSCALL_1 sys_chpass, 25
SYSCALL_0 sys_getuid, 26
SYSCALL_1 sys_setuid, 27
SYSCALL_1 sys_authenticate, 28
SYSCALL_0 sys_shutdown, 29
SYSCALL_0 sys_restart, 30
SYSCALL_3 sys_get_mem_stats, 31
SYSCALL_2 sys_exec, 32
SYSCALL_0 sys_fork, 33
SYSCALL_2 sys_get_procs, 34
SYSCALL_1 sys_kill, 35
SYSCALL_1 sys_sleep, 36
SYSCALL_0 sys_get_ticks, 37
SYSCALL_0 sys_kbhit, 38
SYSCALL_2 sys_wait, 39
SYSCALL_2 sys_get_username, 40
SYSCALL_2 sys_chmod, 41
SYSCALL_2 sys_dup2, 42
SYSCALL_1 sys_pipe, 43
SYSCALL_0 sys_getgid, 44
SYSCALL_1 sys_setgid, 45
SYSCALL_4 sys_draw_char_at, 46
SYSCALL_4 sys_draw_string_at, 47
SYSCALL_2 sys_update_cursor, 48
