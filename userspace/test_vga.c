#include <punix.h>

int main() {
    printf("Attempting to write directly to VGA memory (0xB8000)...\n");
    printf("If protection is working, this should trigger a Page Fault.\n");
    
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    *vga = 0x0741; // Attempt to write 'A' with light grey color
    
    printf("ERROR: Protection failed! I was able to write to 0xB8000.\n");
    exit(1);
}
