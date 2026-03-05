#ifndef PUNIX_DEF_H
#define PUNIX_DEF_H

// FS Constants
#define FS_MAX_NAME       60
#define FS_MAX_INODES     256

// File flags
#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02
#define O_CREAT   0x04
#define O_TRUNC   0x08
#define O_APPEND  0x10

// FS Types
#define FS_TYPE_FILE      0x01
#define FS_TYPE_DIRECTORY 0x02

// Permission bits
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001

// VGA Colors
#define COLOR_BLACK          0x0
#define COLOR_BLUE           0x1
#define COLOR_GREEN          0x2
#define COLOR_CYAN           0x3
#define COLOR_RED            0x4
#define COLOR_MAGENTA        0x5
#define COLOR_BROWN          0x6
#define COLOR_LIGHT_GREY     0x7
#define COLOR_DARK_GREY      0x8
#define COLOR_LIGHT_BLUE     0x9
#define COLOR_LIGHT_GREEN    0xA
#define COLOR_LIGHT_CYAN     0xB
#define COLOR_LIGHT_RED      0xC
#define COLOR_LIGHT_MAGENTA  0xD
#define COLOR_LIGHT_BROWN    0xE
#define COLOR_WHITE          0xF

#define COLOR_WHITE_ON_BLACK 0x0F
#define COLOR_GREEN_ON_BLACK 0x02

#endif
