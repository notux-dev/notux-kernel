#ifndef XV6FS_API_H
#define XV6FS_API_H
#include "types.h"
struct inode;
struct inode* namei(char *path);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
void iput(struct inode *ip);
void iupdate(struct inode *ip);
int readi(struct inode *ip, char *dst, uint off, uint n);
int writei(struct inode *ip, char *src, uint off, uint n);
struct inode* xv6fs_create(char *path, short type, short major, short minor);
int xv6fs_unlink(char *path);
#endif
