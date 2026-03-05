#include <punix.h>
#include <stdio.h>

int main() {
    uint32_t total, used, free;
    sys_get_mem_stats(&total, &used, &free);

    printf("=== Memory Statistics ===\n");
    printf("Total RAM: %d KB\n", (total * 4096) / 1024);
    printf("Used RAM:  %d KB\n", (used * 4096) / 1024);
    printf("Free RAM:  %d KB\n", (free * 4096) / 1024);

    uint32_t total_d, used_d, free_d;
    sys_get_disk_stats(&total_d, &used_d, &free_d);
    printf("\n=== Disk Statistics ===\n");
    printf("Total Disk: %d KB\n", total_d);
    printf("Used Disk:  %d KB\n", used_d);
    printf("Free Disk:  %d KB\n", free_d);

    return 0;
}
