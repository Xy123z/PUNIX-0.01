#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include <stdint.h>

void* malloc(size_t size);
void  free(void* ptr);

int atoi(const char* str);
char* itoa(int value, char* str, int base);

void exit(int status);

long strtol(const char* nptr, char** endptr, int base);

#endif
