#ifndef STDIO_H
#define STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#define EOF (-1)

int printf(const char* format, ...);
int sprintf(char* str, const char* format, ...);
int vsprintf(char* str, const char* format, va_list ap);

int puts(const char* s);
int putchar(int c);
int getchar(void);

// System-specific / Extended I/O
void print(const char* str);
void print_colored(const char* str, uint8_t color);

#endif
