#ifndef SYS_IOCTL_H
#define SYS_IOCTL_H

#include <stdint.h>

typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} winsize_t;

// ioctl request numbers
#define TCGETS       0x5401
#define TCSETS       0x5402
#define TCSETSW      0x5403
#define TIOCGWINSZ   0x5413
#define TIOCSWINSZ   0x5414
#define TIOCSCTTY    0x540E
#define TIOCGPGRP    0x540F
#define TIOCSPGRP    0x5410

int ioctl(int fd, uint32_t request, void* argp);

#endif
