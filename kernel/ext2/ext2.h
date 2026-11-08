#ifndef EXT2_H
#define EXT2_H
#include <stdint.h>
#include "ahci/ahci.h"
#ifndef EXT2_UINT
#define EXT2_UINT
typedef unsigned int uint;
#endif

#define EXT2_DEV      1
#define EXT2_ROOT_INO 2
#define T_LNK         4

struct inode;
int  ext2_mount(ahci_device_t *dev);
int  ext2_is_mounted(void);
int  ext2_load(struct inode *ip);
int  ext2_readi(struct inode *ip, char *dst, uint off, uint n);
struct inode *ext2_dirlookup(struct inode *dp, const char *name, int len);
int  ext2_readdir(struct inode *dp, uint *pos, char *name, int *namelen, uint *ino, int *dtype);
int  ext2_readlink(struct inode *ip, char *buf, int max);
void ext2_statfs(uint *blocks, uint *bfree, uint *files, uint *ffree, uint *bsize);
int  ext2_writei(struct inode *ip, const char *src, uint off, uint n);
int  ext2_truncate(struct inode *ip, uint len);
int  ext2_iupdate(struct inode *ip);
void ext2_evict(struct inode *ip);
int  ext2_create(struct inode *dp, const char *name, int len, uint mode, struct inode **out);
int  ext2_mkdir(struct inode *dp, const char *name, int len, uint mode, struct inode **out);
int  ext2_symlink(struct inode *dp, const char *name, int len, const char *target, int tlen);
int  ext2_unlink(struct inode *dp, const char *name, int len, int want);
int  ext2_link(struct inode *dp, const char *name, int len, struct inode *ip);
int  ext2_rename(struct inode *odp, const char *on, int ol, struct inode *ndp, const char *nn, int nl);
int  ext2_setattr(struct inode *ip, int64_t mode, int64_t uid, int64_t gid, int64_t atime, int64_t mtime);

void ext2_set_mountpoint(uint xv6_inum);
int  ext2_mountpoint(void);
void ext2_set_root_mode(int on);
int  ext2_is_root(void);
void ext2_set_bind(uint ext2_ino, uint xv6_mp);
uint ext2_bind_ino(void);
uint ext2_bind_mp(void);
#endif
