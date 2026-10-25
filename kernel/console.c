#include "console.h"
#include "font.h"
#include "gfx.h"
#include "vbe.h"
#define SCREEN_W 800
#define SCREEN_H 600
#define CHAR_W 8
#define CHAR_H 16
#define COLS (SCREEN_W / CHAR_W)
#define ROWS (SCREEN_H / CHAR_H)
static int cursor_x = 0;
static int cursor_y = 0;
void cinit(void) {
    cursor_x = 0;
    cursor_y = 0;
}
static void scroll(void) {
    volatile uint32_t *fb = (volatile uint32_t *)(uint64_t)vbeaddr();
    int row_pixels = SCREEN_W * CHAR_H;
    for (int i = 0; i < row_pixels * (ROWS - 1); i++) {
        fb[i] = fb[i + row_pixels];
    }
    for (int i = row_pixels * (ROWS - 1); i < row_pixels * ROWS; i++) {
        fb[i] = 0xFFFFFFFF;
    }
    cursor_y--;
}
static void newline(void) {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= ROWS) {
        scroll();
    }
}
void kprint(const char *s, uint32_t color) {
    while (*s) {
        if (*s == '\n') {
            newline();
            s++;
            continue;
        }
        drawchar(cursor_x * CHAR_W, cursor_y * CHAR_H, *s, color);
        cursor_x++;
        if (cursor_x >= COLS) {
            newline();
        }
        s++;
    }
}
void kprintn(const char *s, size_t len, uint32_t color) {
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            newline();
            continue;
        }
        drawchar(cursor_x * CHAR_W, cursor_y * CHAR_H, s[i], color);
        cursor_x++;
        if (cursor_x >= COLS) {
            newline();
        }
    }
}
int console_get_col(void) { return cursor_x; }
int console_get_row(void) { return cursor_y; }
void console_set_pos(int col, int row) {
    cursor_x = col;
    cursor_y = row;
    while (cursor_y >= ROWS) {
        scroll();
    }
}
