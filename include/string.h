// include/string.h - String utility functions

#ifndef STRING_H
#define STRING_H

#include "types.h" // Assumed to contain size_t, uint32_t, etc.

// Memory functions
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
// String functions
int strcmp(const char* str1, const char* str2);
void strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
int strlen(const char* str);
void strcat(char* dest, const char* source);
void int_to_str(int num, char* str);
void int_to_hex(uint32_t num, char* str);
int str_to_int(const char* str);

#endif // STRING_H
