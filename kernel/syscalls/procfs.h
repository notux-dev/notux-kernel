#ifndef SYSCALLS_PROCFS_H
#define SYSCALLS_PROCFS_H
#include "stdint.h"
#include "process.h"
#include "kfile.h"


#define PROC_NOTMINE (-200000)

int proc_stat(process_t *p, const char *path, uint32_t *mode, uint64_t *ino);
int64_t proc_open(process_t *p, const char *path, int flags);
int64_t proc_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n);
void proc_release(struct kfile *f);
int proc_f_is_dir(struct kfile *f);
int64_t proc_getdents(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t count);
#endif
