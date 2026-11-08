#include "vga.h"
void print(const char *msg, int row, char color) {
    volatile char *video = (volatile char*)0xB8000;
    int offset = row * 160;
    for (int i = 0; msg[i]; i++) {
        video[offset + i*2] = msg[i];
        video[offset + i*2 + 1] = color;
    }
}
