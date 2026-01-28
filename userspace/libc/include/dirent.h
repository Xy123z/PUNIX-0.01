#ifndef DIRENT_H
#define DIRENT_H

#include <stdint.h>

struct dirent {
    uint32_t d_ino;
    uint8_t  d_type;
    char     d_name[64];
};

#endif
