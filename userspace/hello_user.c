// userspace/hello_user.c - Fully self-contained user program
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

// System call wrappers
int sys_print(const char* str);
int sys_read(int fd, void* buf, uint32_t count);
char sys_getchar();
void sys_putchar(char c);
void sys_exit(int status);
__attribute__((section(".text.entry")))
void _start() {
    sys_print("========================================\n");
    sys_print("  PUNIX STANDALONE USER TEST PROGRAM #2   \n");
    sys_print("========================================\n");
    sys_print("Syscall sys_print: OK\n");
    char c;
    do{
    sys_print("Press any key to continue(q for quitting): \n");

    c = sys_getchar();   // BLOCKS here

    sys_print("You pressed: ");
    sys_putchar(c);
    sys_print("\n");
    }
    while(c != 'q');
    sys_print("exiting program\n");
    sys_exit(0);
}

// Assembly wrappers for system calls
__asm__(
    ".global sys_print\n"
    "sys_print:\n"
    "   push %ebx\n"
    "   mov 8(%esp), %ebx\n"    // str
    "   mov $15, %eax\n"        // SYS_PRINT
    "   int $0x80\n"
    "   pop %ebx\n"
    "   ret\n"

    ".global sys_read\n"
    "sys_read:\n"
    "   push %ebx\n"
    "   mov 8(%esp), %ebx\n"    // fd
    "   mov 12(%esp), %ecx\n"   // buf
    "   mov 16(%esp), %edx\n"   // count
    "   mov $0, %eax\n"         // SYS_READ
    "   int $0x80\n"
    "   pop %ebx\n"
    "   ret\n"
    ".global sys_getchar\n"
    "sys_getchar:\n"
    "   mov $17, %eax\n"      // SYS_GETCHAR
    "   int $0x80\n"
    "   ret\n"

    ".global sys_putchar\n"
    "sys_putchar:\n"
    "   mov 4(%esp), %ebx\n" // char c
    "   mov $18, %eax\n"     // SYS_PUTCHAR
    "   int $0x80\n"
    "   ret\n"

    ".global sys_exit\n"
    "sys_exit:\n"
    "push %ebx\n"
    "mov 8(%esp), %ebx\n"
    "mov $11, %eax\n"
    "int $0x80\n"
    "pop %ebx\n"
    "ret\n"
);
