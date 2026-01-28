#include <punix.h>
#include <stdio.h>
#include <string.h>

void print_perms(uint32_t mode, uint8_t type) {
    char perm[11];
    perm[0] = (type == FS_TYPE_DIRECTORY) ? 'd' : '-';
    perm[1] = (mode & 0400) ? 'r' : '-';
    perm[2] = (mode & 0200) ? 'w' : '-';
    perm[3] = (mode & 0100) ? 'x' : '-';
    perm[4] = (mode & 0040) ? 'r' : '-';
    perm[5] = (mode & 0020) ? 'w' : '-';
    perm[6] = (mode & 0010) ? 'x' : '-';
    perm[7] = (mode & 0004) ? 'r' : '-';
    perm[8] = (mode & 0002) ? 'w' : '-';
    perm[9] = (mode & 0001) ? 'x' : '-';
    perm[10] = '\0';
    printf("%s ", perm);
}

int main(int argc, char** argv) {
    char path[256];
    if (argc > 1) {
        strcpy(path, argv[1]);
    } else {
        strcpy(path, ".");
    }

    struct dirent entries[32];
    int count = sys_getdents(path, entries, 32);

    if (count < 0) {
        printf("ls: cannot access '%s'\n", path);
        return 1;
    }

    for (int i = 0; i < count; i++) {
        char full_path[512];
        if (strcmp(path, "/") == 0) {
            sprintf(full_path, "/%s", entries[i].d_name);
        } else {
            sprintf(full_path, "%s/%s", path, entries[i].d_name);
        }

        struct_stat_t st;
        if (stat(full_path, &st) == 0) { // Using stat wrapper if available
            print_perms(st.st_mode, st.st_type);
            printf("%d\t%s\n", st.st_size, entries[i].d_name);
        } else {
            printf("??????????\t%s\n", entries[i].d_name);
        }
    }

    if (count == 0 && argc > 1) {
        // Check if it's a file
        struct_stat_t st;
        if (sys_stat(path, &st) == 0) {
            print_perms(st.st_mode, st.st_type);
            printf("%d\t%s\n", st.st_size, path);
        }
    }

    return 0;
}
