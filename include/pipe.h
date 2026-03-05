#ifndef PIPE_H
#define PIPE_H

#include "types.h"

#define PIPE_BUF_SIZE 4096

typedef struct {
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t size;
    uint32_t readers;
    uint32_t writers;
} pipe_t;

pipe_t* pipe_create();
void pipe_destroy(pipe_t* pipe);
int pipe_read(pipe_t* pipe, uint8_t* buf, uint32_t count);
int pipe_write(pipe_t* pipe, const uint8_t* buf, uint32_t count);

#endif
