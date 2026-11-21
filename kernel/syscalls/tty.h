#ifndef SYSCALLS_TTY_H
#define SYSCALLS_TTY_H 
#include "stdint.h"
#include "process.h"
#include "kfile.h"
void tty_init(void);
void tty_input(uint8_t c);
int64_t tty_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n, void **chan);
int64_t tty_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
void tty_write_kernel(const char *s, uint64_t n);
int tty_poll(struct kfile *f);
int tty_nread(struct kfile *f);
int64_t tty_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg);
int tty_kbd_on(void);
void tty_vt_release(struct kfile *f);
int pty_open_master(struct kfile *f);
int pty_stat(const char *path, uint32_t *mode, uint64_t *ino, uint64_t *rdev, uint32_t *uid);
int64_t pty_open_slave_path(process_t *p, const char *path, int flags);
void pty_attach_ctty(process_t *p, struct kfile *f);
void pty_slave_close(struct kfile *f);
void pty_master_close(struct kfile *f);
int64_t pty_master_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t pty_master_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int pty_master_poll(struct kfile *f);
int pty_slave_poll(struct kfile *f);
int64_t pty_master_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg);
void *pty_master_chan(struct kfile *f);
void *tty_wait_chan(struct kfile *f, int wr);
extern char tty_chan;
#define DEV_MEM_MAJOR 1
#define DEV_TTY_MAJOR 4
#define PTS_NOTMINE 0x7fff0001
#define DEV_CON_MAJOR 5
#endif
