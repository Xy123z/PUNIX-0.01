#include <punix.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: kill PID\n");
        return 1;
    }

    int pid = atoi(argv[1]);
    if (sys_kill(pid) == 0) {
        printf("Process %d killed.\n", pid);
        return 0;
    } else {
        printf("kill: failed to kill process %d\n", pid);
        return 1;
    }
}
