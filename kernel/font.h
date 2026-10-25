#ifndef FONT_H
#define FONT_H
#include "types.h"
#define CHAR_W 8
#define CHAR_H 8
void drawchar(int x, int y, char c, uint32_t color);
void drawstring(int x, int y, const char *s, uint32_t color);
#endif
