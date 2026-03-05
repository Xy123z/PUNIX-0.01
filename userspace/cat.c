#include <punix.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: cat FILE\n");
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("cat: %s: No such file or directory\n", argv[1]);
        return 1;
    }

    char buf[1024];
    int read_bytes;
    while ((read_bytes = read(fd, buf, sizeof(buf))) > 0) {
        // We don't have a sys_write to stdout exactly, 
        // but sys_write(1, ...) is redirected in syscall.c
        write(1, buf, read_bytes);
    }

    close(fd);
    return 0;
}
