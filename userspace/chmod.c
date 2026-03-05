#include <punix.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: chmod MODE FILE\n");
        return 1;
    }

    uint32_t mode = strtol(argv[1], NULL, 8);
    if (chmod(argv[2], mode) == 0) {
        return 0;
    } else {
        printf("chmod: failed to change mode of '%s'\n", argv[2]);
        return 1;
    }
}
