#ifndef XV6_BUF_H
#define XV6_BUF_H
#include "fs.h"
struct buf {
    int flags;
    uint dev;
    uint blockno;
    struct buf *prev;
    struct buf *next;
    struct buf *qnext;
    uint8_t data[BSIZE];
};
#define B_VALID 0x2
#define B_DIRTY 0x4
#endif
