#ifndef SERIAL_H
#define SERIAL_H
#include "stdint.h"
void serinit(void);
void serwrite(const char *msg);
void serputc(char c);
int serrx_ready(void);
uint8_t serrx_get(void);
#endif
