#ifndef PANIC_H
#define PANIC_H
#include "stdint.h"
void panic(const char *message) __attribute__((noreturn));
#endif
