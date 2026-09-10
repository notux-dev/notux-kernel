#include "font.h"
#include "font8x8.h" 
#include "gfx.h"
void drawchar(int x, int y, char c, uint32_t color) {
    int index;
    if (c >= 0x20 && c <= 0x7E)
        index = c - 0x20; 
    else
        index = 0; 
    const uint8_t *glyph = font_8x8[index]; 
    for (int row = 0; row < 8; row++) { 
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                putpixel(x + col, y + row, color);
            }
        }
    }
}
void drawstring(int x, int y, const char *s, uint32_t color) {
    int cx = x;
    while (*s) {
        drawchar(cx, y, *s, color);
        cx += 8; 
        s++;
    }
}
