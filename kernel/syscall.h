#ifndef SYSCALL_H
#define SYSCALL_H
#include "types.h"
void syscall_handler(uint64_t *regs);
void syscall_init(void);
#endif
