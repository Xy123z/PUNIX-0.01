#include <punix.h>
#include <stdio.h>
#include <string.h>

int main() {
    proc_info_t procs[32];
    int count = sys_get_procs(procs, 32);
    
    printf("PID  PPID  STATE       NAME\n");
    printf("---  ----  -----       ----\n");
    for (int i = 0; i < count; i++) {
        const char* state_str;
        switch (procs[i].state) {
            case 0: state_str = "NEW    "; break;
            case 1: state_str = "READY  "; break;
            case 2: state_str = "RUNNING"; break;
            case 3: state_str = "WAITING"; break;
            case 4: state_str = "IO     "; break;
            case 5: state_str = "TERMINATED"; break;
            case 6: state_str = "BACKGRD"; break;
            case 7: state_str = "ZOMBIE "; break;
            default: state_str = "UNKNOWN"; break;
        }
        printf("%d    %d     %s    %s\n", procs[i].pid, procs[i].ppid, state_str, procs[i].name);
    }
    return 0;
}
