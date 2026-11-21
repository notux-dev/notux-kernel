#include "console.h"
#include "font.h"
#include "gfx.h"
#include "vbe.h"
#define CHAR_W 8
#define CHAR_H 16
#define MAX_COLS (VBE_MAX_W / CHAR_W)
#define MAX_ROWS (VBE_MAX_H / CHAR_H)
static int COLS = 100;
static int ROWS = 37;
static int SCREEN_W = 800;
static int SCREEN_H = 600;
static int PITCHPX = 800;
static int gfx_hold = 0;
static int cursor_x = 0;
static int cursor_y = 0;
static void geom_update(void) {
    SCREEN_W = (int)vbe_width();
    SCREEN_H = (int)vbe_height();
    PITCHPX = (int)(vbe_pitch() / 4);
    COLS = SCREEN_W / CHAR_W;
    ROWS = SCREEN_H / CHAR_H;
    if (COLS > MAX_COLS) COLS = MAX_COLS;
    if (ROWS > MAX_ROWS) ROWS = MAX_ROWS;
}
void cinit(void) {
    geom_update();
    cursor_x = 0;
    cursor_y = 0;
}
static void scroll(void) {
    if (gfx_hold) { cursor_y--; return; }
    volatile uint32_t *fb = (volatile uint32_t *)(uint64_t)vbeaddr();
    int row_pixels = PITCHPX * CHAR_H;
    for (int i = 0; i < row_pixels * (ROWS - 1); i++) {
        fb[i] = fb[i + row_pixels];
    }
    for (int y = CHAR_H * (ROWS - 1); y < CHAR_H * ROWS; y++)
        for (int x = 0; x < SCREEN_W; x++)
            fb[y * PITCHPX + x] = 0xFFFFFFFF;
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
    if (gfx_hold) return;
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
    if (gfx_hold) return;
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
static char cell[MAX_ROWS][MAX_COLS];
static int cursor_on = 0;
int console_cols(void) { return COLS; }
int console_rows(void) { return ROWS; }
static void cell_invert(int cx, int cy)
{
    if (gfx_hold) return;
    volatile uint32_t *fb = (volatile uint32_t *)(uint64_t)vbeaddr();
    for (int y = 0; y < CHAR_H; y++)
        for (int x = 0; x < CHAR_W; x++)
            fb[(cy * CHAR_H + y) * PITCHPX + cx * CHAR_W + x] ^= 0x00FFFFFF;
}
void console_cursor(int on)
{
    if (on == cursor_on)
        return;
    if (cursor_x >= 0 && cursor_x < COLS && cursor_y >= 0 && cursor_y < ROWS)
        cell_invert(cursor_x, cursor_y);
    cursor_on = on;
}
static void cell_draw(int cx, int cy)
{
    if (gfx_hold) return;
    volatile uint32_t *fb = (volatile uint32_t *)(uint64_t)vbeaddr();
    for (int y = 0; y < CHAR_H; y++)
        for (int x = 0; x < CHAR_W; x++)
            fb[(cy * CHAR_H + y) * PITCHPX + cx * CHAR_W + x] = 0xFFFFFFFF;
    char c = cell[cy][cx];
    if (c > ' ')
        drawchar(cx * CHAR_W, cy * CHAR_H, c, 0x00000000);
}
static void grid_scroll(void)
{
    scroll();
    for (int y = 0; y + 1 < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            cell[y][x] = cell[y + 1][x];
    for (int x = 0; x < COLS; x++)
        cell[ROWS - 1][x] = ' ';
}
void console_cr(void) { cursor_x = 0; }
void console_bs(void) { if (cursor_x > 0) cursor_x--; }
void console_lf(void)
{
    cursor_y++;
    if (cursor_y >= ROWS)
        grid_scroll();
}
void console_set_cursor(int col, int row)
{
    if (col < 0) col = 0;
    if (col >= COLS) col = COLS - 1;
    if (row < 0) row = 0;
    if (row >= ROWS) row = ROWS - 1;
    cursor_x = col;
    cursor_y = row;
}
void console_putc(char c)
{
    if (cursor_x >= COLS)
    {
        cursor_x = 0;
        console_lf();
    }
    cell[cursor_y][cursor_x] = c;
    cell_draw(cursor_x, cursor_y);
    cursor_x++;
}
void console_erase_line(int mode)
{
    int a = mode == 0 ? cursor_x : 0;
    int b = mode == 1 ? cursor_x : COLS - 1;
    for (int x = a; x <= b && x < COLS; x++)
    {
        cell[cursor_y][x] = ' ';
        cell_draw(x, cursor_y);
    }
}
void console_erase_screen(int mode)
{
    int sy = mode == 0 ? cursor_y : 0;
    int ey = mode == 1 ? cursor_y : ROWS - 1;
    for (int y = sy; y <= ey; y++)
        for (int x = 0; x < COLS; x++)
        {
            if (y == cursor_y && ((mode == 0 && x < cursor_x) || (mode == 1 && x > cursor_x)))
                continue;
            cell[y][x] = ' ';
            cell_draw(x, y);
        }
}
int display_set_mode(unsigned w, unsigned h)
{
    if (!vbe_set_mode(w, h))
        return 0;
    geom_update();
    vbefill(0x00FFFFFF);
    for (int y = 0; y < MAX_ROWS; y++)
        for (int x = 0; x < MAX_COLS; x++)
            cell[y][x] = ' ';
    cursor_on = 0;
    cursor_x = 0;
    cursor_y = 0;
    return 1;
}
void console_gfx_hold(int d)
{
    int was = gfx_hold;
    gfx_hold += d;
    if (gfx_hold < 0)
        gfx_hold = 0;
    if (was && !gfx_hold)
    {
        geom_update();
        vbefill(0x00FFFFFF);
        int co = cursor_on;
        cursor_on = 0;
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                if (cell[y][x] > ' ')
                    cell_draw(x, y);
        if (co)
            console_cursor(1);
    }
}
