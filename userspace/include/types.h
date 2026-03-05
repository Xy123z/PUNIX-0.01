#ifndef TYPES_H
#define TYPES_H

typedef unsigned int   uint32_t;
typedef int            int32_t;
typedef unsigned short uint16_t;
typedef short          int16_t;
typedef unsigned char  uint8_t;
typedef char           int8_t;
typedef unsigned int   size_t;

#ifndef NULL
#define NULL 0
#endif

typedef struct {
    uint32_t pid;
    uint32_t state;
    char name[32];
} proc_info_t;

#endif
