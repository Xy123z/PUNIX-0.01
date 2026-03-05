// src/mouse.c - PS/2 Mouse Driver Implementation (Minimal Scroll Handler)
#include "../include/mouse.h"
#include "../include/console.h" // To interact with the console scroll state
#include "../include/types.h"

// NOTE: This implementation assumes the PS/2 controller has been initialized
// to read the 4-byte scroll wheel packet (which often requires special commands).

// Placeholder function - This logic belongs in your actual PS/2 interrupt handler
void mouse_handle_packet(uint8_t packet_byte0, uint8_t packet_byte1, uint8_t packet_byte2, uint8_t packet_byte3) {
    // packet_byte3 contains the scroll information (Z-axis)
    int scroll_delta = 0;

    // Bit 3 of byte 3 is the sign bit for the Z-axis (scroll)
    // Bits 0, 1, 2 determine the magnitude of the movement.
    // For simplicity, we check if there's any movement in the 4th byte:

    if (packet_byte3 != 0x00) {
        // Check sign bit (Bit 3 of byte 3)
        if (packet_byte3 & 0x08) {
            // Scroll Down (Negative delta)
            scroll_delta = -1;
        } else {
            // Scroll Up (Positive delta)
            scroll_delta = 1;
        }
    }

    if (scroll_delta != 0) {
        // We scroll by 3 lines per 'tick' for better movement, common in Linux/Windows
        mouse_handle_scroll(scroll_delta * 3);
    }

    // Handle X, Y movement and button clicks here...
}


// Function to pass scroll data to the console logic
void mouse_handle_scroll(int scroll_delta) {
    // Forward the scroll command to the module responsible for displaying content
    console_scroll_by(scroll_delta);
}

void mouse_init() {
    // Driver initialization logic here (e.g., enabling interrupts, setting 4-byte packet mode)
}
