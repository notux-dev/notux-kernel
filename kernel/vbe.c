#include "vbe.h"
#include "io.h"
#include "pci.h"
#include "stdint.h"
#define VBE_INDEX 0x1CE
#define VBE_DATA 0x1CF
#define VBE_ID 0
#define VBE_XRES 1
#define VBE_YRES 2
#define VBE_BPP 3
#define VBE_ENABLE 4
#define VBE_VIRT_W 6
#define VBE_VRAM64K 0xA
uint32_t vbe_g_addr = 0, vbe_g_w = 800, vbe_g_h = 600, vbe_g_pitch = 800 * 4;
#define fb_addr vbe_g_addr
#define fb_w vbe_g_w
#define fb_h vbe_g_h
#define fb_pitch vbe_g_pitch
static int bochs = -1;
static void vbewrite(uint16_t index, uint16_t value) {
    outw(VBE_INDEX, index);
    outw(VBE_DATA, value);
}
static uint16_t vberead(uint16_t index) {
    outw(VBE_INDEX, index);
    return inw(VBE_DATA);
}
static int is_bochs(void) {
    if (bochs < 0) {
        uint16_t id = vberead(VBE_ID);
        bochs = (id >= 0xB0C0 && id <= 0xB0CF) ? 1 : 0;
    }
    return bochs;
}
void vbemode(uint16_t width, uint16_t height, uint16_t bpp) {
    vbewrite(VBE_ENABLE, 0);
    vbewrite(VBE_XRES, width);
    vbewrite(VBE_YRES, height);
    vbewrite(VBE_BPP, bpp);
    vbewrite(VBE_ENABLE, 0x01 | 0x40);
    fb_addr = pcibar0();
    fb_w = width; fb_h = height; fb_pitch = (uint32_t)width * (bpp / 8);
}
void vbefill(uint32_t color) {
    volatile uint32_t *fb = (volatile uint32_t*)(uintptr_t)fb_addr;
    uint32_t pp = fb_pitch / 4;
    for (uint32_t y = 0; y < fb_h; y++)
        for (uint32_t x = 0; x < fb_w; x++)
            fb[y * pp + x] = color;
}
uint32_t vbeaddr(void) { return fb_addr; }
uint32_t vbe_width(void) { return fb_w; }
uint32_t vbe_height(void) { return fb_h; }
uint32_t vbe_pitch(void) { return fb_pitch; }
int vbe_can_set_mode(void) { return is_bochs(); }
uint32_t vbe_vram_bytes(void) {
    if (is_bochs()) {
        uint32_t v = (uint32_t)vberead(VBE_VRAM64K) * 65536u;
        if (v) return v;
    }
    return fb_pitch * fb_h;
}
int vbe_set_mode(uint32_t w, uint32_t h) {
    if (!is_bochs() || !fb_addr)
        return 0;
    if (w < 320 || h < 200 || w > VBE_MAX_W || h > VBE_MAX_H || (w & 7))
        return 0;
    if ((uint64_t)w * h * 4 > vbe_vram_bytes())
        return 0;
    vbewrite(VBE_ENABLE, 0);
    vbewrite(VBE_XRES, (uint16_t)w);
    vbewrite(VBE_YRES, (uint16_t)h);
    vbewrite(VBE_BPP, 32);
    vbewrite(VBE_ENABLE, 0x01 | 0x40);
    uint32_t rw = vberead(VBE_XRES), rh = vberead(VBE_YRES), vw = vberead(VBE_VIRT_W);
    if (rw != w || rh != h)
        return 0;
    fb_w = w; fb_h = h;
    fb_pitch = (vw >= w ? vw : w) * 4;
    return 1;
}
int vbe_from_bootloader(uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint8_t bpp, uint8_t type)
{
    if (type != 1 || bpp != 32 || w < 320 || h < 200 || w > VBE_MAX_W || h > VBE_MAX_H || pitch < w * 4)
        return 0;
    if (addr == 0 || addr + (uint64_t)pitch * h > 0x100000000ULL)
        return 0;
    fb_addr = (uint32_t)addr;
    fb_w = w; fb_h = h; fb_pitch = pitch;
    return 1;
}
