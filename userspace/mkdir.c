#include <punix.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: mkdir DIRECTORY\n");
        return 1;
    }

    if (mkdir(argv[1]) == 0) {
        return 0;
    } else {
        printf("mkdir: failed to create directory '%s'\n", argv[1]);
        return 1;
    }
}
