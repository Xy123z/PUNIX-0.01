#include <punix.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: rmdir DIRECTORY\n");
        return 1;
    }

    if (rmdir(argv[1]) == 0) {
        return 0;
    } else {
        printf("rmdir: failed to remove directory '%s' (is it empty?)\n", argv[1]);
        return 1;
    }
}
