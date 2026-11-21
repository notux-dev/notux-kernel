#ifndef SYSCALLS_KFILE_H
#define SYSCALLS_KFILE_H 
#include "stdint.h"
#include "process.h"
enum { FT_NONE = 0, FT_CONSOLE, FT_INODE, FT_PIPE_R, FT_PIPE_W, FT_PROC, FT_NULL, FT_ZERO, FT_RANDOM, FT_SOCK,
       FT_EVENT, FT_TIMER, FT_SIGNAL, FT_EPOLL, FT_EVDEV, FT_FB, FT_PTM };
#define E_BLOCK (-100000)
struct kpipe;
struct ksock;
struct inode;
struct kfile {
    int refs;
    int type;
    int flags;
    uint64_t off;
    struct inode *ip;
    struct kpipe *pipe;
    struct ksock *sock;
    char *pbuf;
    uint32_t plen;
    int pkind;
    uint32_t ppid;
    uint64_t ev_a, ev_b;
    int ev_flag;
    void *obj;
};
int path_last_too_long(const char *path);
struct kfile *kfile_alloc(int type, int flags, struct inode *ip);
void kfile_put(struct kfile *f);
int fd_install(process_t *p, struct kfile *f, int minfd, int cloexec);
int64_t sock_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t sock_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
void sock_close(struct kfile *f);
void *sock_chan(struct kfile *f);
int sock_poll(struct kfile *f);
int sock_nread(struct kfile *f);
int ev_poll(struct kfile *f);
int64_t ev_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t ev_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
void *ev_chan(struct kfile *f);
int evdev_open(struct kfile *f, int minor);
void evdev_close(struct kfile *f);
int evdev_poll(struct kfile *f);
int64_t evdev_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t evdev_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t evdev_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg);
extern char input_chan;
int64_t fb_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t fb_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t fb_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg);
int64_t fb_mmap(process_t *p, struct kfile *f, uint64_t addr, uint64_t len, uint64_t prot, uint64_t flags, uint64_t off);
int64_t fb_seek(struct kfile *f, int64_t off, int whence);
void fb_close(struct kfile *f);
void ev_close(struct kfile *f);
void epoll_forget(struct kfile *f);
struct kpipe *pipe_create(void);
int64_t pipe_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t pipe_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
void pipe_close_end(struct kfile *f);
int pipe_poll(struct kfile *f);
int pipe_nread(struct kfile *f);
extern char poll_chan;
int kfile_poll(struct kfile *f);
int kfd_poll(process_t *p, int64_t fd);
int64_t kfd_rw(process_t *p, int64_t fd, int wr, uint64_t ubuf, uint64_t n, void **chan);
void fdt_init(process_t *p);
void fdt_clone(process_t *parent, process_t *child);
void fdt_close_all(process_t *p);
void fdt_close_cloexec(process_t *p);
struct kfile *fd_get(process_t *p, int64_t fd);
struct inode *fd_inode_of(process_t *p, int64_t fd);
uint64_t *fd_offset(process_t *p, int64_t fd);
#endif
