#include <punix.h>
#include <stdio.h>

int main() {
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        printf("Error: Could not get current directory\n");
        return 1;
    }
    return 0;
}
