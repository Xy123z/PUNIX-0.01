#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

// Initialize the PS/2 mouse device
void mouse_init();

// Handler called by the PS/2 interrupt to process a scroll event
// Scrolls the console view by the specified number of lines (e.g., 3 lines per tick)
void mouse_handle_scroll(int scroll_delta);

// Placeholder for the main interrupt logic that extracts the 4th byte (scroll data)
void mouse_handle_packet(uint8_t packet_byte0, uint8_t packet_byte1, uint8_t packet_byte2, uint8_t packet_byte3);

#endif // MOUSE_H
