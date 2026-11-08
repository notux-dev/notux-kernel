#include "vbe.h"
#include "io.h"
#include "pci.h"
#include "stdint.h"
#define VBE_INDEX 0x1CE
#define VBE_DATA  0x1CF
static uint32_t fb_addr = 0;
static void vbewrite(uint16_t index, uint16_t value) {
    outw(VBE_INDEX, index);
    outw(VBE_DATA, value);
}
void vbemode(uint16_t width, uint16_t height, uint16_t bpp) {
    vbewrite(4, 0);
    vbewrite(1, width);
    vbewrite(2, height);
    vbewrite(3, bpp);
    vbewrite(4, 0x01 | 0x40);
    fb_addr = pcibar0();
}
void vbefill(uint32_t color) {
    volatile uint32_t *fb = (volatile uint32_t*)(uintptr_t)fb_addr;
    for (int i = 0; i < 800 * 600; i++) {
        fb[i] = color;
    }
}
uint32_t vbeaddr(void) {
    return fb_addr;
}

int vbe_from_bootloader(uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint8_t bpp, uint8_t type)
{
    if (type != 1 || bpp != 32 || w != 800 || h != 600 || pitch != 800 * 4)
        return 0;
    if (addr == 0 || addr + (uint64_t)pitch * h > 0x100000000ULL)
        return 0;
    fb_addr = (uint32_t)addr;
    return 1;
}
