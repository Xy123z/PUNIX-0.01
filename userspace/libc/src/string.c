#include "../include/string.h"

size_t strlen(const char* str) {
    size_t len = 0;
    while (str && str[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src) {
    if (!dest || !src) return dest;
    size_t i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    if (!dest || !src) return dest;
    size_t i = 0;
    while (i < n && src[i]) {
        dest[i] = src[i];
        i++;
    }
    while (i < n) {
        dest[i] = '\0';
        i++;
    }
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (n > 1 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

char* strcat(char* dest, const char* src) {
    if (!dest || !src) return dest;
    char* p = dest + strlen(dest);
    while (*src) {
        *p++ = *src++;
    }
    *p = '\0';
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    if (!dest || !src) return dest;
    char* p = dest + strlen(dest);
    while (n-- && *src) {
        *p++ = *src++;
    }
    *p = '\0';
    return dest;
}

char* strchr(const char* s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return NULL;
}

char* strrchr(const char* s, int c) {
    if (!s) return NULL;
    char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = (char*)s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return last;
}

void int_to_str(int n, char* str) {
    int i = 0;
    int is_negative = 0;
    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    if (n < 0) {
        is_negative = 1;
        n = -n;
    }
    while (n != 0) {
        str[i++] = (n % 10) + '0';
        n = n / 10;
    }
    if (is_negative) str[i++] = '-';
    str[i] = '\0';
    // Reverse
    for (int j = 0; j < i / 2; j++) {
        char temp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = temp;
    }
}

int str_to_int(const char* str) {
    int res = 0;
    int sign = 1;
    int i = 0;
    if (!str) return 0;
    if (str[0] == '-') {
        sign = -1;
        i++;
    }
    for (; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break;
        res = res * 10 + str[i] - '0';
    }
    return sign * res;
}

void int_to_hex(uint32_t n, char* str) {
    char* hex_chars = "0123456789ABCDEF";
    int i = 0;
    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    while (n != 0) {
        str[i++] = hex_chars[n % 16];
        n /= 16;
    }
    str[i] = '\0';
    // Reverse
    for (int j = 0; j < i / 2; j++) {
        char temp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = temp;
    }
}
