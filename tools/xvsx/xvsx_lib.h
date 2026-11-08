#ifndef XVSX_LIB_H
#define XVSX_LIB_H
#include <stdint.h>
#include <stddef.h>
#include "xvsx_format.h"

typedef struct {
    uint32_t lblock, len, pblock;
} xext_t;

typedef struct xvsx {
    int fd;
    int rw;
    struct xvsx_super sb;
    uint8_t *ibm;
    uint8_t *bbm;
    uint32_t max_run;
    uint64_t hint;
    int64_t now;
} xvsx_t;

int  xvsx_open(xvsx_t *x, const char *path, int rw);
int  xvsx_close(xvsx_t *x);
int  xvsx_format(xvsx_t *x, const char *path, uint64_t bytes,
                 const char *label);
int  xvsx_read_block(xvsx_t *x, uint64_t blk, void *buf);
int  xvsx_write_block(xvsx_t *x, uint64_t blk, const void *buf);
void xvsx_sb_seal(struct xvsx_super *sb);
int  xvsx_sb_check(const struct xvsx_super *sb);

int  xvsx_test_bit(const uint8_t *bm, uint64_t i);
void xvsx_set_bit(uint8_t *bm, uint64_t i);
void xvsx_clear_bit(uint8_t *bm, uint64_t i);

int  xvsx_inode_read(xvsx_t *x, uint32_t ino, struct xvsx_inode *in);
int  xvsx_inode_write(xvsx_t *x, uint32_t ino, struct xvsx_inode *in);
uint32_t xvsx_inode_crc(const struct xvsx_inode *in);
uint32_t xvsx_alloc_inode(xvsx_t *x, uint16_t mode);

uint32_t xvsx_alloc_block(xvsx_t *x);
uint32_t xvsx_alloc_run(xvsx_t *x, uint32_t want, uint32_t *got);
void xvsx_free_block(xvsx_t *x, uint32_t blk);

int  xvsx_ext_load(xvsx_t *x, const struct xvsx_inode *in, xext_t *out, int max);
int  xvsx_ext_store(xvsx_t *x, struct xvsx_inode *in, const xext_t *e, int n);
uint32_t xvsx_ext_map(const xext_t *e, int n, uint32_t lblock);

int  xvsx_file_write(xvsx_t *x, uint32_t ino, const void *data, uint64_t len);
int64_t xvsx_file_read(xvsx_t *x, uint32_t ino, void **out);

int  xvsx_dir_add(xvsx_t *x, uint32_t dir, const char *name, uint32_t ino,
                  uint8_t type);
uint32_t xvsx_dir_lookup(xvsx_t *x, uint32_t dir, const char *name);
uint32_t xvsx_lookup(xvsx_t *x, const char *path);
uint32_t xvsx_mkdir(xvsx_t *x, uint32_t parent, const char *name, uint16_t mode);
uint32_t xvsx_mkfile(xvsx_t *x, uint32_t parent, const char *name,
                     const void *data, uint64_t len, uint16_t mode);
uint32_t xvsx_mksymlink(xvsx_t *x, uint32_t parent, const char *name,
                        const char *target);
uint8_t xvsx_mode_to_dt(uint16_t mode);

typedef int (*xvsx_dir_cb)(void *ctx, uint32_t ino, uint8_t type,
                           const char *name, int name_len);
int  xvsx_dir_walk(xvsx_t *x, uint32_t dir, xvsx_dir_cb cb, void *ctx);

#endif
