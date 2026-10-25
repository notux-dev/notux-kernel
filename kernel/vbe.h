#ifndef VBE_H
#define VBE_H
#include "types.h"
void vbemode(uint16_t width, uint16_t height, uint16_t bpp);
void vbefill(uint32_t color);
uint32_t vbeaddr(void);
#endif
