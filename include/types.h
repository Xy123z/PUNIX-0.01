// include/types.h - Standard types and definitions

#ifndef TYPES_H
#define TYPES_H

// Standard integer types
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef uint32_t size_t;

// Boolean type
typedef int bool;
#define true 1
#define false 0

// NULL pointer
#define NULL ((void*)0)

// Common macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    char name[32];
} proc_info_t;

#endif // TYPES_H
