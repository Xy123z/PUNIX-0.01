#include "../include/math.h"
uint32_t min(uint32_t a, uint32_t b){
    if(a<b)
        return a;
    return b;
}
uint32_t max(uint32_t a, uint32_t b){
    if(a>b)
        return a;
    return b;
}
// Example math.c implementation
uint32_t abs_diff(uint32_t a, uint32_t b) {
    if (a > b) {
        return a - b;
    } else {
        return b - a;
    }
}
