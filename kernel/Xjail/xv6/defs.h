#ifndef XV6_DEFS_H
#define XV6_DEFS_H
#include <stdint.h>
#include "fs.h"
#include "buf.h"
#include "file.h"
void initlock(struct spinlock*, const char*);
void acquire(struct spinlock*);
void release(struct spinlock*);
void initsleeplock(struct sleeplock*, const char*);
void acquiresleep(struct sleeplock*);
void releasesleep(struct sleeplock*);
int  holdingsleep(struct sleeplock*);
struct buf* bread(int dev, uint32_t blockno);
void        brelse(struct buf *bp);
void        log_write(struct buf *bp);
void        panic(const char*) __attribute__((noreturn));
void        cprintf(const char*, ...);
void*       memmove(void*, const void*, uint);
void*       memset(void*, int, uint);
int         strncmp(const char*, const char*, uint);
char*       strncpy(char*, const char*, int);
struct proc* myproc(void);
#endif
