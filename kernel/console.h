#ifndef CONSOLE_H
#define CONSOLE_H
#include "types.h"
void cinit(void);
void kprint(const char *s, uint32_t color);
void kprintn(const char *s, size_t len, uint32_t color);
int console_get_col(void);
int console_get_row(void);
void console_set_pos(int col, int row);
#endif
