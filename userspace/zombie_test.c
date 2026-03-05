#include <punix.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    printf("Zombie Test: Forking child...\n");
    
    int pid = fork();
    
    if (pid < 0) {
        printf("Fork failed!\n");
        return 1;
    }
    
    if (pid == 0) {
        // Child process
        printf("Child (PID %d): I will sleep for 3 seconds and then exit to become a zombie.\n", getpid());
             sleep(3);
        printf("Child (PID %d): Exiting now. I expect to be reaped by init.\n", getpid());
        exit(42); // Exit with status 42
    } else {
        // Parent process
        printf("Parent (PID %d): I will exit immediately WITHOUT waiting for my child (PID %d).\n", getpid(), pid);

        printf("Parent (PID %d): The child should now be orphaned and adopted by init (PID 1).\n", getpid());
        exit(0);
    }
    
}
