#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"
#include "../include/punix.h"

extern void* sys_malloc(size_t size);
extern void  sys_free(void* ptr);

void* malloc(size_t size) {
    return sys_malloc(size);
}

void free(void* ptr) {
    sys_free(ptr);
}

int atoi(const char* str) {
    return str_to_int(str);
}

char* itoa(int value, char* str, int base) {
    if (base == 10) {
        int_to_str(value, str);
    } else if (base == 16) {
        int_to_hex((uint32_t)value, str);
    } else {
        // Fallback or others not implemented
        str[0] = '0';
        str[1] = '\0';
    }
    return str;
}

// exit is often in unistd too, but stdlib is common
void exit(int status) {
    sys_exit(status);
}

long strtol(const char* nptr, char** endptr, int base) {
    long res = 0;
    int i = 0;
    while (nptr[i] == ' ' || nptr[i] == '\t') i++;
    
    int neg = 0;
    if (nptr[i] == '-') { neg = 1; i++; }
    else if (nptr[i] == '+') { i++; }

    while (nptr[i]) {
        int v = -1;
        if (nptr[i] >= '0' && nptr[i] <= '9') v = nptr[i] - '0';
        else if (nptr[i] >= 'a' && nptr[i] <= 'z') v = nptr[i] - 'a' + 10;
        else if (nptr[i] >= 'A' && nptr[i] <= 'Z') v = nptr[i] - 'A' + 10;
        
        if (v == -1 || v >= base) break;
        res = res * base + v;
        i++;
    }
    
    if (endptr) *endptr = (char*)(nptr + i);
    return neg ? -res : res;
}
