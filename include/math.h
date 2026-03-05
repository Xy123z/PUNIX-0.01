#ifndef MATH_H
#define MATH_H

#include "types.h"

// Simple integer ceiling division: (a + b - 1) / b
#define ceil_div(a, b) ((a + b - 1) / b)

// Basic utility functions
uint32_t min(uint32_t a, uint32_t b);
uint32_t max(uint32_t a, uint32_t b);
uint32_t abs_diff(uint32_t a, uint32_t b);

#endif // MATH_H
