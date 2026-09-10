#include "gfx.h"
#include "vbe.h"
#define SCREEN_W 800
void putpixel(int x, int y, uint32_t color) {
    volatile uint32_t *fb = (volatile uint32_t *)(uint64_t)vbeaddr();
    fb[y * SCREEN_W + x] = color;
}
