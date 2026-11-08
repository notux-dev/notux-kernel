#ifndef VBE_H
#define VBE_H
#include "types.h"
void vbemode(uint16_t width, uint16_t height, uint16_t bpp);
void vbefill(uint32_t color);
uint32_t vbeaddr(void);
int vbe_from_bootloader(uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint8_t bpp, uint8_t type);
#endif
