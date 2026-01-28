#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>

size_t strlen(const char* str);
char*  strcpy(char* dest, const char* src);
char*  strncpy(char* dest, const char* src, size_t n);
int    strcmp(const char* s1, const char* s2);
int    strncmp(const char* s1, const char* s2, size_t n);
void*  memset(void* s, int c, size_t n);
void*  memcpy(void* dest, const void* src, size_t n);
void*  memmove(void* dest, const void* src, size_t n);
int    memcmp(const void* s1, const void* s2, size_t n);
char*  strcat(char* dest, const char* src);
char*  strncat(char* dest, const char* src, size_t n);
char*  strchr(const char* s, int c);
char*  strrchr(const char* s, int c);
char*  strstr(const char* haystack, const char* needle);
char*  strdup(const char* s);

// Integer to string helpers (standard-ish but useful)
void   int_to_str(int n, char* str);
int    str_to_int(const char* str);
void   int_to_hex(uint32_t n, char* str);

#endif
