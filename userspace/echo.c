#include <punix.h>
#include <stdio.h>
#include <string.h> // Added for strlen

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        write(1, argv[i], strlen(argv[i]));
        if (i < argc - 1) write(1, " ", 1);
    }
    write(1, "\n", 1);
    return 0;
}
