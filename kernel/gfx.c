#include "gfx.h"
#include "vbe.h"
void putpixel(int x, int y, uint32_t color) {
    if ((uint32_t)x >= vbe_g_w || (uint32_t)y >= vbe_g_h)
        return;
    ((volatile uint32_t *)(uint64_t)vbe_g_addr)[(uint32_t)y * (vbe_g_pitch >> 2) + (uint32_t)x] = color;
}
