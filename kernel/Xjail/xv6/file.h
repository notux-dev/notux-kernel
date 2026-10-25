#ifndef XV6_FILE_H
#define XV6_FILE_H
#include "fs.h"
#include "sleeplock.h"
#include "types.h"
struct inode {
    uint dev;
    uint inum;
    int ref;
    int flags;
    short type;
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT+2];
    struct sleeplock lock;
    int valid;
};
struct file {
    enum { FD_NONE, FD_PIPE, FD_INODE } type;
    int ref;
    char readable;
    char writable;
    struct inode *ip;
    uint off;
};
struct stat {
    short type;
    int dev;
    uint ino;
    short nlink;
    uint size;
};
struct devsw {
    int (*read)(struct inode*, char*, int);
    int (*write)(struct inode*, char*, int);
};
extern struct devsw devsw[];
#define T_DIR  1   
#define T_FILE 2   
#define T_DEV  3   
#endif
