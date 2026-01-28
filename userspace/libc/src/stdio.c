#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/unistd.h"

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int puts(const char* s) {
    if (!s) return -1;
    write(1, s, strlen(s));
    putchar('\n');
    return 0;
}

// Minimal vsprintf for printf/sprintf
int vsprintf(char* str, const char* format, va_list ap) {
    char* p = str;
    const char* f = format;
    
    while (*f) {
        if (*f == '%') {
            f++;
            // Handle padding/width (not fully implemented, just basics)
            int width = 0;
            while (*f >= '0' && *f <= '9') {
                width = width * 10 + (*f - '0');
                f++;
            }
            
            switch (*f) {
                case 's': {
                    char* s = va_arg(ap, char*);
                    if (!s) s = "(null)";
                    while (*s) *p++ = *s++;
                    break;
                }
                case 'd': {
                    int d = va_arg(ap, int);
                    char buf[12];
                    int_to_str(d, buf);
                    char* s = buf;
                    while (*s) *p++ = *s++;
                    break;
                }
                case 'x': {
                    uint32_t x = va_arg(ap, uint32_t);
                    char buf[12];
                    int_to_hex(x, buf);
                    char* s = buf;
                    while (*s) *p++ = *s++;
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    *p++ = c;
                    break;
                }
                case '%': {
                    *p++ = '%';
                    break;
                }
                default:
                    *p++ = *f;
                    break;
            }
        } else {
            *p++ = *f;
        }
        f++;
    }
    *p = '\0';
    return p - str;
}

int printf(const char* format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int n = vsprintf(buf, format, ap);
    va_end(ap);
    write(1, buf, n);
    return n;
}

int sprintf(char* str, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int n = vsprintf(str, format, ap);
    va_end(ap);
    return n;
}

// System specific helpers
extern void sys_print(const char* str);
extern void sys_print_colored(const char* str, uint8_t color);

void print(const char* str) {
    if (str) write(1, str, strlen(str));
}

void print_colored(const char* str, uint8_t color) {
    // Note: color support currently relies on SYS_PRINT_COLORED or ANSI
    // We can use ANSI if we want to be fully compliant, 
    // or keep sys_print_colored if kernel still supports it.
    // Let's use ANSI for maximum compliance if possible, 
    // but sys_print_colored is fine for internal kernel colors.
    sys_print_colored(str, color);
}

int getchar(void) {
    char c;
    if (read(0, &c, 1) <= 0) return EOF;
    return (int)c;
}
