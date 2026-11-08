#ifndef SYSCALLS_TTY_H
#define SYSCALLS_TTY_H
#include "stdint.h"
#include "process.h"
#include "kfile.h"


void tty_init(void);
void tty_input(uint8_t c);
int64_t tty_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n, void **chan);
int64_t tty_write(process_t *p, uint64_t ubuf, uint64_t n);
void tty_write_kernel(const char *s, uint64_t n);
int tty_poll(void);
int tty_nread(void);
int64_t tty_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg);
extern char tty_chan;

#define DEV_MEM_MAJOR 1
#define DEV_TTY_MAJOR 4
#define DEV_CON_MAJOR 5
#endif
