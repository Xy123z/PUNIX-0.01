#ifndef STDIO_H
#define STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#define EOF (-1)

int printf(const char* format, ...);
int sprintf(char* str, const char* format, ...);
int vsprintf(char* str, const char* format, va_list ap);
char* fgets(char* s, int size, void* stream);

#define stdin ((void*)0)
#define stdout ((void*)1)
#define stderr ((void*)2)

int puts(const char* s);
int fflush(void* stream);
int putchar(int c);
int getchar(void);

// System-specific / Extended I/O
void print(const char* str);
void print_colored(const char* str, uint8_t color);

#endif
