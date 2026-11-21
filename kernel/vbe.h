#ifndef VBE_H
#define VBE_H 
#include "types.h"
extern uint32_t vbe_g_addr, vbe_g_w, vbe_g_h, vbe_g_pitch;
void vbemode(uint16_t width, uint16_t height, uint16_t bpp);
void vbefill(uint32_t color);
uint32_t vbeaddr(void);
uint32_t vbe_width(void);
uint32_t vbe_height(void);
uint32_t vbe_pitch(void);
uint32_t vbe_vram_bytes(void);
int vbe_can_set_mode(void);
int vbe_set_mode(uint32_t w, uint32_t h);
#define VBE_MAX_W 4096
#define VBE_MAX_H 2048
int vbe_from_bootloader(uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint8_t bpp, uint8_t type);
#endif
