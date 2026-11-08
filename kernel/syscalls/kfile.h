#ifndef SYSCALLS_KFILE_H
#define SYSCALLS_KFILE_H
#include "stdint.h"
#include "process.h"

enum { FT_NONE = 0, FT_CONSOLE, FT_INODE, FT_PIPE_R, FT_PIPE_W, FT_PROC, FT_NULL, FT_ZERO, FT_RANDOM };

#define E_BLOCK (-100000)
struct kpipe;

struct inode;
struct kfile {
    int      refs;
    int      type;
    int      flags;
    uint64_t off;
    struct inode *ip;
    struct kpipe *pipe;
    char    *pbuf;
    uint32_t plen;
    int      pkind;
    uint32_t ppid;
};

int path_last_too_long(const char *path);
struct kfile *kfile_alloc(int type, int flags, struct inode *ip);
void kfile_put(struct kfile *f);
int fd_install(process_t *p, struct kfile *f, int minfd, int cloexec);

struct kpipe *pipe_create(void);
int64_t pipe_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
int64_t pipe_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
void pipe_close_end(struct kfile *f);
int pipe_poll(struct kfile *f);
int pipe_nread(struct kfile *f);
extern char poll_chan;
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
