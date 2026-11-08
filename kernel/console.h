#ifndef CONSOLE_H
#define CONSOLE_H
#include "types.h"
void cinit(void);
void kprint(const char *s, uint32_t color);
void kprintn(const char *s, size_t len, uint32_t color);
int console_get_col(void);
int console_get_row(void);
void console_set_pos(int col, int row);
int console_cols(void);
int console_rows(void);
void console_cursor(int on);
void console_cr(void);
void console_bs(void);
void console_lf(void);
void console_set_cursor(int col, int row);
void console_putc(char c);
void console_erase_line(int mode);
void console_erase_screen(int mode);
#endif
