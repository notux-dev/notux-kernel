#define _GNU_SOURCE
#include "xvsx_lib.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "xvsXfs tools assume a little-endian host"
#endif

static uint32_t crc_tab[256];
static int crc_ready;
uint32_t xvsx_crc32c(uint32_t seed, const void *data, uint32_t len)
{
    if (!crc_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (c >> 1) ^ 0x82F63B78u : c >> 1;
            crc_tab[i] = c;
        }
        crc_ready = 1;
    }
    uint32_t c = ~seed;
    const uint8_t *p = data;
    while (len--)
        c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

int xvsx_test_bit(const uint8_t *bm, uint64_t i) { return (bm[i >> 3] >> (i & 7)) & 1; }
void xvsx_set_bit(uint8_t *bm, uint64_t i)       { bm[i >> 3] |= (uint8_t)(1u << (i & 7)); }
void xvsx_clear_bit(uint8_t *bm, uint64_t i)     { bm[i >> 3] &= (uint8_t)~(1u << (i & 7)); }

static int64_t now_time(void)
{
    const char *e = getenv("SOURCE_DATE_EPOCH");
    return e ? (int64_t)strtoll(e, 0, 10) : (int64_t)time(NULL);
}

int xvsx_read_block(xvsx_t *x, uint64_t blk, void *buf)
{
    if (blk >= x->sb.total_blocks && x->sb.total_blocks)
        return -1;
    return pread(x->fd, buf, XVSX_BLOCK_SIZE, (off_t)(blk * XVSX_BLOCK_SIZE)) ==
                   XVSX_BLOCK_SIZE ? 0 : -1;
}
int xvsx_write_block(xvsx_t *x, uint64_t blk, const void *buf)
{
    if (!x->rw || blk >= x->sb.total_blocks)
        return -1;
    return pwrite(x->fd, buf, XVSX_BLOCK_SIZE, (off_t)(blk * XVSX_BLOCK_SIZE)) ==
                   XVSX_BLOCK_SIZE ? 0 : -1;
}

void xvsx_sb_seal(struct xvsx_super *sb)
{
    sb->csum = 0;
    sb->csum = xvsx_crc32c(0, sb, XVSX_BLOCK_SIZE - 4);
}

int xvsx_sb_check(const struct xvsx_super *sb)
{
    if (memcmp(sb->magic, XVSX_MAGIC, 8)) return -1;
    if (sb->csum != xvsx_crc32c(0, sb, XVSX_BLOCK_SIZE - 4)) return -2;
    if (sb->version_major != XVSX_VERSION_MAJOR) return -3;
    if (sb->block_size != XVSX_BLOCK_SIZE || sb->inode_size != XVSX_INODE_SIZE) return -4;
    if (sb->feature_incompat & ~XVSX_INCOMPAT_KNOWN) return -5;
    return 0;
}

int xvsx_open(xvsx_t *x, const char *path, int rw)
{
    memset(x, 0, sizeof *x);
    x->rw = rw;
    x->now = now_time();
    x->fd = open(path, rw ? O_RDWR : O_RDONLY);
    if (x->fd < 0)
        return -1;
    uint8_t buf[XVSX_BLOCK_SIZE];
    if (pread(x->fd, buf, XVSX_BLOCK_SIZE, XVSX_SB_BLOCK * XVSX_BLOCK_SIZE) !=
        XVSX_BLOCK_SIZE) {
        close(x->fd);
        return -1;
    }
    memcpy(&x->sb, buf, sizeof x->sb);
    int r = xvsx_sb_check(&x->sb);
    if (r) {
        close(x->fd);
        return r;
    }
    size_t ib = (size_t)x->sb.ibitmap_blocks * XVSX_BLOCK_SIZE;
    size_t bb = (size_t)x->sb.bbitmap_blocks * XVSX_BLOCK_SIZE;
    x->ibm = calloc(1, ib ? ib : 1);
    x->bbm = calloc(1, bb ? bb : 1);
    if (!x->ibm || !x->bbm)
        return -1;
    for (uint32_t i = 0; i < x->sb.ibitmap_blocks; i++)
        if (xvsx_read_block(x, x->sb.ibitmap_start + i, x->ibm + (size_t)i * XVSX_BLOCK_SIZE))
            return -1;
    for (uint32_t i = 0; i < x->sb.bbitmap_blocks; i++)
        if (xvsx_read_block(x, x->sb.bbitmap_start + i, x->bbm + (size_t)i * XVSX_BLOCK_SIZE))
            return -1;
    return 0;
}

static int flush(xvsx_t *x)
{
    for (uint32_t i = 0; i < x->sb.ibitmap_blocks; i++)
        if (xvsx_write_block(x, x->sb.ibitmap_start + i, x->ibm + (size_t)i * XVSX_BLOCK_SIZE))
            return -1;
    for (uint32_t i = 0; i < x->sb.bbitmap_blocks; i++)
        if (xvsx_write_block(x, x->sb.bbitmap_start + i, x->bbm + (size_t)i * XVSX_BLOCK_SIZE))
            return -1;
    x->sb.state = XVSX_STATE_CLEAN;
    xvsx_sb_seal(&x->sb);
    if (xvsx_write_block(x, XVSX_SB_BLOCK, &x->sb))
        return -1;
    return fsync(x->fd);
}

int xvsx_close(xvsx_t *x)
{
    int r = 0;
    if (x->rw)
        r = flush(x);
    close(x->fd);
    free(x->ibm);
    free(x->bbm);
    x->ibm = x->bbm = 0;
    return r;
}

uint32_t xvsx_inode_crc(const struct xvsx_inode *in)
{
    struct xvsx_inode t = *in;
    t.csum = 0;
    return xvsx_crc32c(0, &t, sizeof t);
}

static int ino_loc(xvsx_t *x, uint32_t ino, uint64_t *blk, uint32_t *off)
{
    if (ino < 1 || ino > x->sb.total_inodes)
        return -1;
    *blk = x->sb.itable_start + (ino - 1) / XVSX_INODES_PER_BLOCK;
    *off = ((ino - 1) % XVSX_INODES_PER_BLOCK) * XVSX_INODE_SIZE;
    return 0;
}

int xvsx_inode_read(xvsx_t *x, uint32_t ino, struct xvsx_inode *in)
{
    uint64_t blk; uint32_t off;
    uint8_t buf[XVSX_BLOCK_SIZE];
    if (ino_loc(x, ino, &blk, &off) || xvsx_read_block(x, blk, buf))
        return -1;
    memcpy(in, buf + off, sizeof *in);
    return in->csum == xvsx_inode_crc(in) ? 0 : -2;
}

int xvsx_inode_write(xvsx_t *x, uint32_t ino, struct xvsx_inode *in)
{
    uint64_t blk; uint32_t off;
    uint8_t buf[XVSX_BLOCK_SIZE];
    if (ino_loc(x, ino, &blk, &off) || xvsx_read_block(x, blk, buf))
        return -1;
    in->csum = xvsx_inode_crc(in);
    memcpy(buf + off, in, sizeof *in);
    return xvsx_write_block(x, blk, buf);
}

uint32_t xvsx_alloc_inode(xvsx_t *x, uint16_t mode)
{
    for (uint32_t i = 0; i < x->sb.total_inodes; i++) {
        if (xvsx_test_bit(x->ibm, i))
            continue;
        xvsx_set_bit(x->ibm, i);
        x->sb.free_inodes--;
        struct xvsx_inode in;
        memset(&in, 0, sizeof in);
        in.mode = mode;
        in.nlink = 0;
        in.atime = in.mtime = in.ctime = in.crtime = x->now;
        in.generation = 1;
        if (xvsx_inode_write(x, i + 1, &in))
            return 0;
        return i + 1;
    }
    return 0;
}

uint32_t xvsx_alloc_block(xvsx_t *x)
{
    for (uint64_t b = x->sb.data_start; b < x->sb.total_blocks; b++) {
        if (xvsx_test_bit(x->bbm, b))
            continue;
        xvsx_set_bit(x->bbm, b);
        x->sb.free_blocks--;
        uint8_t z[XVSX_BLOCK_SIZE];
        memset(z, 0, sizeof z);
        if (xvsx_write_block(x, b, z))
            return 0;
        return (uint32_t)b;
    }
    return 0;
}

uint32_t xvsx_alloc_run(xvsx_t *x, uint32_t want, uint32_t *got)
{
    uint32_t cap = x->max_run ? x->max_run : 0xFFFFFFFFu;
    if (want > cap)
        want = cap;
    uint64_t from = x->sb.data_start;
    if (x->max_run && x->hint > from)
        from = x->hint;
    for (uint64_t b = from; b < x->sb.total_blocks; b++) {
        if (xvsx_test_bit(x->bbm, b))
            continue;
        uint32_t n = 0;
        while (n < want && b + n < x->sb.total_blocks &&
               !xvsx_test_bit(x->bbm, b + n)) {
            xvsx_set_bit(x->bbm, b + n);
            n++;
        }
        x->sb.free_blocks -= n;
        *got = n;
        if (x->max_run)
            x->hint = b + n + 1;
        return (uint32_t)b;
    }
    return 0;
}

void xvsx_free_block(xvsx_t *x, uint32_t blk)
{
    if (blk >= x->sb.data_start && blk < x->sb.total_blocks &&
        xvsx_test_bit(x->bbm, blk)) {
        xvsx_clear_bit(x->bbm, blk);
        x->sb.free_blocks++;
    }
}

static int ext_walk(xvsx_t *x, const uint8_t *buf, xext_t *out, int max,
                    int *n, int level)
{
    const struct xvsx_ext_header *h = (const void *)buf;
    if (h->magic != XVSX_EXT_MAGIC || h->entries > h->max ||
        level > XVSX_EXT_MAX_DEPTH)
        return -1;
    const struct xvsx_ext *e = (const void *)(buf + sizeof *h);
    if (h->depth == 0) {
        for (int i = 0; i < h->entries; i++) {
            if (*n >= max)
                return -1;
            out[(*n)++] = (xext_t){e[i].lblock, e[i].len, e[i].pblock};
        }
        return 0;
    }
    for (int i = 0; i < h->entries; i++) {
        uint8_t child[XVSX_BLOCK_SIZE];
        if (xvsx_read_block(x, e[i].len, child))
            return -1;
        if (ext_walk(x, child, out, max, n, level + 1))
            return -1;
    }
    return 0;
}

int xvsx_ext_load(xvsx_t *x, const struct xvsx_inode *in, xext_t *out, int max)
{
    if (!(in->flags & XVSX_IF_EXTENTS))
        return 0;
    int n = 0;
    return ext_walk(x, in->data, out, max, &n, 0) ? -1 : n;
}

static void free_nodes(xvsx_t *x, const uint8_t *buf, int level)
{
    const struct xvsx_ext_header *h = (const void *)buf;
    if (h->magic != XVSX_EXT_MAGIC || h->depth == 0 || level > XVSX_EXT_MAX_DEPTH)
        return;
    const struct xvsx_ext *e = (const void *)(buf + sizeof *h);
    for (int i = 0; i < h->entries && i < XVSX_EXT_NODE_MAX; i++) {
        uint8_t child[XVSX_BLOCK_SIZE];
        if (xvsx_read_block(x, e[i].len, child) == 0)
            free_nodes(x, child, level + 1);
        xvsx_free_block(x, e[i].len);
    }
}

int xvsx_ext_store(xvsx_t *x, struct xvsx_inode *in, const xext_t *e, int n)
{
    if (in->flags & XVSX_IF_EXTENTS)
        free_nodes(x, in->data, 0);
    memset(in->data, 0, sizeof in->data);
    in->flags &= (uint16_t)~XVSX_IF_EXTENTS;
    uint64_t blocks = 0;
    for (int i = 0; i < n; i++)
        blocks += e[i].len;
    if (n == 0) {
        in->blocks = 0;
        return 0;
    }
    struct xvsx_ext_header *h = (void *)in->data;
    struct xvsx_ext *ent = (void *)(in->data + sizeof *h);
    h->magic = XVSX_EXT_MAGIC;
    h->max = XVSX_EXT_ROOT_MAX;
    if (n <= XVSX_EXT_ROOT_MAX) {
        h->depth = 0;
        h->entries = (uint16_t)n;
        for (int i = 0; i < n; i++)
            ent[i] = (struct xvsx_ext){e[i].lblock, e[i].len, e[i].pblock};
    } else {
        int leaves = (n + XVSX_EXT_NODE_MAX - 1) / XVSX_EXT_NODE_MAX;
        if (leaves > XVSX_EXT_ROOT_MAX)
            return -1;
        h->depth = 1;
        h->entries = (uint16_t)leaves;
        for (int l = 0; l < leaves; l++) {
            uint32_t blk = xvsx_alloc_block(x);
            if (!blk)
                return -1;
            uint8_t node[XVSX_BLOCK_SIZE];
            memset(node, 0, sizeof node);
            struct xvsx_ext_header *nh = (void *)node;
            struct xvsx_ext *ne = (void *)(node + sizeof *nh);
            int from = l * XVSX_EXT_NODE_MAX;
            int cnt = n - from < XVSX_EXT_NODE_MAX ? n - from : XVSX_EXT_NODE_MAX;
            nh->magic = XVSX_EXT_MAGIC;
            nh->max = XVSX_EXT_NODE_MAX;
            nh->depth = 0;
            nh->entries = (uint16_t)cnt;
            for (int i = 0; i < cnt; i++)
                ne[i] = (struct xvsx_ext){e[from + i].lblock, e[from + i].len,
                                          e[from + i].pblock};
            *(uint32_t *)(node + XVSX_EXT_NODE_CSUM_OFF) =
                xvsx_crc32c(0, node, XVSX_EXT_NODE_CSUM_OFF);
            if (xvsx_write_block(x, blk, node))
                return -1;
            ent[l] = (struct xvsx_ext){e[from].lblock, blk, 0};
            blocks++;
        }
    }
    in->flags |= XVSX_IF_EXTENTS;
    in->blocks = blocks;
    return 0;
}

uint32_t xvsx_ext_map(const xext_t *e, int n, uint32_t lblock)
{
    for (int i = 0; i < n; i++)
        if (lblock >= e[i].lblock && lblock - e[i].lblock < e[i].len)
            return e[i].pblock + (lblock - e[i].lblock);
    return 0;
}

#define MAXEXT 8192

static int release_content(xvsx_t *x, struct xvsx_inode *in)
{
    if (in->flags & XVSX_IF_EXTENTS) {
        xext_t *e = malloc(sizeof(xext_t) * MAXEXT);
        if (!e)
            return -1;
        int n = xvsx_ext_load(x, in, e, MAXEXT);
        if (n < 0) { free(e); return -1; }
        for (int i = 0; i < n; i++)
            for (uint32_t j = 0; j < e[i].len; j++)
                xvsx_free_block(x, e[i].pblock + j);
        free(e);
        xvsx_ext_store(x, in, 0, 0);
    }
    memset(in->data, 0, sizeof in->data);
    in->flags = 0;
    in->size = 0;
    in->blocks = 0;
    return 0;
}

int xvsx_file_write(xvsx_t *x, uint32_t ino, const void *data, uint64_t len)
{
    struct xvsx_inode in;
    if (xvsx_inode_read(x, ino, &in))
        return -1;
    if (release_content(x, &in))
        return -1;
    uint16_t type = in.mode & XVSX_S_IFMT;
    if (len == 0) {
    } else if ((type == XVSX_S_IFREG || type == XVSX_S_IFLNK) &&
               len <= XVSX_INLINE_MAX) {
        memcpy(in.data, data, len);
        in.flags |= XVSX_IF_INLINE;
    } else {
        xext_t *e = malloc(sizeof(xext_t) * MAXEXT);
        if (!e)
            return -1;
        int n = 0;
        uint64_t nb = (len + XVSX_BLOCK_SIZE - 1) / XVSX_BLOCK_SIZE, done = 0;
        const uint8_t *src = data;
        while (done < nb) {
            uint32_t got = 0;
            uint32_t run = xvsx_alloc_run(x, (uint32_t)(nb - done > 0x7FFFFFFF ?
                                                         0x7FFFFFFF : nb - done), &got);
            if (!run || n >= MAXEXT) {
                for (int i = 0; i < n; i++)
                    for (uint32_t j = 0; j < e[i].len; j++)
                        xvsx_free_block(x, e[i].pblock + j);
                free(e);
                return -1;
            }
            for (uint32_t j = 0; j < got; j++) {
                uint8_t blk[XVSX_BLOCK_SIZE];
                memset(blk, 0, sizeof blk);
                uint64_t off = (done + j) * XVSX_BLOCK_SIZE;
                uint64_t take = len - off < XVSX_BLOCK_SIZE ? len - off : XVSX_BLOCK_SIZE;
                memcpy(blk, src + off, take);
                if (xvsx_write_block(x, run + j, blk)) { free(e); return -1; }
            }
            if (n && e[n - 1].pblock + e[n - 1].len == run &&
                e[n - 1].lblock + e[n - 1].len == done)
                e[n - 1].len += got;
            else
                e[n++] = (xext_t){(uint32_t)done, got, run};
            done += got;
        }
        int r = xvsx_ext_store(x, &in, e, n);
        free(e);
        if (r)
            return -1;
    }
    in.size = len;
    in.mtime = in.ctime = x->now;
    return xvsx_inode_write(x, ino, &in);
}

int64_t xvsx_file_read(xvsx_t *x, uint32_t ino, void **out)
{
    struct xvsx_inode in;
    if (xvsx_inode_read(x, ino, &in))
        return -1;
    uint8_t *buf = calloc(1, in.size ? in.size : 1);
    if (!buf)
        return -1;
    if (in.flags & XVSX_IF_INLINE) {
        memcpy(buf, in.data, in.size <= XVSX_INLINE_MAX ? in.size : XVSX_INLINE_MAX);
    } else if (in.size) {
        xext_t *e = malloc(sizeof(xext_t) * MAXEXT);
        int n = e ? xvsx_ext_load(x, &in, e, MAXEXT) : -1;
        if (n < 0) { free(e); free(buf); return -1; }
        uint64_t nb = (in.size + XVSX_BLOCK_SIZE - 1) / XVSX_BLOCK_SIZE;
        for (uint64_t l = 0; l < nb; l++) {
            uint32_t p = xvsx_ext_map(e, n, (uint32_t)l);
            if (!p)
                continue;
            uint8_t blk[XVSX_BLOCK_SIZE];
            if (xvsx_read_block(x, p, blk)) { free(e); free(buf); return -1; }
            uint64_t off = l * XVSX_BLOCK_SIZE;
            uint64_t take = in.size - off < XVSX_BLOCK_SIZE ? in.size - off : XVSX_BLOCK_SIZE;
            memcpy(buf + off, blk, take);
        }
        free(e);
    }
    *out = buf;
    return (int64_t)in.size;
}

uint8_t xvsx_mode_to_dt(uint16_t mode)
{
    switch (mode & XVSX_S_IFMT) {
    case XVSX_S_IFDIR: return XVSX_DT_DIR;
    case XVSX_S_IFREG: return XVSX_DT_REG;
    case XVSX_S_IFLNK: return XVSX_DT_LNK;
    case XVSX_S_IFCHR: return XVSX_DT_CHR;
    case XVSX_S_IFBLK: return XVSX_DT_BLK;
    default: return XVSX_DT_UNKNOWN;
    }
}

static void dirblk_seal(uint8_t *b)
{
    struct xvsx_dirent *t = (void *)(b + XVSX_DIR_TAIL_OFF);
    t->ino = 0;
    t->rec_len = 16;
    t->name_len = 0;
    t->type = XVSX_DT_TAIL;
    *(uint32_t *)(b + 4092) = xvsx_crc32c(0, b, 4092);
}
static void dirblk_init(uint8_t *b)
{
    memset(b, 0, XVSX_BLOCK_SIZE);
    struct xvsx_dirent *d = (void *)b;
    d->ino = 0;
    d->rec_len = XVSX_DIR_FIRST_LEN;
    dirblk_seal(b);
}

static int load_dir(xvsx_t *x, uint32_t dir, struct xvsx_inode *in,
                    xext_t **e, int *n)
{
    if (xvsx_inode_read(x, dir, in) || (in->mode & XVSX_S_IFMT) != XVSX_S_IFDIR)
        return -1;
    *e = malloc(sizeof(xext_t) * MAXEXT);
    if (!*e)
        return -1;
    *n = xvsx_ext_load(x, in, *e, MAXEXT);
    if (*n < 0) { free(*e); return -1; }
    return 0;
}

int xvsx_dir_walk(xvsx_t *x, uint32_t dir, xvsx_dir_cb cb, void *ctx)
{
    struct xvsx_inode in; xext_t *e; int n;
    if (load_dir(x, dir, &in, &e, &n))
        return -1;
    int ret = 0;
    uint64_t nb = in.size / XVSX_BLOCK_SIZE;
    for (uint64_t l = 0; l < nb && !ret; l++) {
        uint32_t p = xvsx_ext_map(e, n, (uint32_t)l);
        uint8_t b[XVSX_BLOCK_SIZE];
        if (!p || xvsx_read_block(x, p, b)) { ret = -1; break; }
        for (uint32_t off = 0; off < XVSX_DIR_FIRST_LEN;) {
            struct xvsx_dirent *d = (void *)(b + off);
            if (d->rec_len < 8 || d->rec_len % 8 || off + d->rec_len > XVSX_DIR_FIRST_LEN) {
                ret = -1;
                break;
            }
            if (d->ino && (ret = cb(ctx, d->ino, d->type, d->name, d->name_len)))
                break;
            off += d->rec_len;
        }
    }
    free(e);
    return ret;
}

struct lk { const char *name; uint32_t ino; };
static int lk_cb(void *c, uint32_t ino, uint8_t t, const char *nm, int nl)
{
    struct lk *k = c;
    (void)t;
    if ((int)strlen(k->name) == nl && !memcmp(k->name, nm, (size_t)nl)) {
        k->ino = ino;
        return 1;
    }
    return 0;
}
uint32_t xvsx_dir_lookup(xvsx_t *x, uint32_t dir, const char *name)
{
    struct lk k = {name, 0};
    xvsx_dir_walk(x, dir, lk_cb, &k);
    return k.ino;
}

uint32_t xvsx_lookup(xvsx_t *x, const char *path)
{
    uint32_t cur = XVSX_ROOT_INO;
    char tmp[1024];
    if (strlen(path) >= sizeof tmp)
        return 0;
    strcpy(tmp, path);
    for (char *s = strtok(tmp, "/"); s; s = strtok(NULL, "/")) {
        cur = xvsx_dir_lookup(x, cur, s);
        if (!cur)
            return 0;
    }
    return cur;
}

int xvsx_dir_add(xvsx_t *x, uint32_t dir, const char *name, uint32_t ino,
                 uint8_t type)
{
    size_t nl = strlen(name);
    if (nl < 1 || nl > XVSX_NAME_MAX || strchr(name, '/'))
        return -1;
    if (xvsx_dir_lookup(x, dir, name))
        return -2;
    struct xvsx_inode in; xext_t *e; int n;
    if (load_dir(x, dir, &in, &e, &n))
        return -1;
    int need = XVSX_DIRENT_LEN((int)nl);
    uint64_t nb = in.size / XVSX_BLOCK_SIZE;
    for (uint64_t l = 0; l <= nb; l++) {
        uint8_t b[XVSX_BLOCK_SIZE];
        uint32_t p;
        int fresh = 0;
        if (l < nb) {
            p = xvsx_ext_map(e, n, (uint32_t)l);
            if (!p || xvsx_read_block(x, p, b)) { free(e); return -1; }
        } else {
            p = xvsx_alloc_block(x);
            if (!p) { free(e); return -1; }
            dirblk_init(b);
            fresh = 1;
        }
        for (uint32_t off = 0; off < XVSX_DIR_FIRST_LEN;) {
            struct xvsx_dirent *d = (void *)(b + off);
            if (d->rec_len < 8 || d->rec_len % 8 || off + d->rec_len > XVSX_DIR_FIRST_LEN) {
                free(e);
                return -1;
            }
            struct xvsx_dirent *slot = 0;
            if (!d->ino && d->rec_len >= need) {
                slot = d;
            } else if (d->ino) {
                int used = XVSX_DIRENT_LEN(d->name_len);
                if (d->rec_len - used >= need) {
                    slot = (void *)(b + off + used);
                    slot->rec_len = (uint16_t)(d->rec_len - used);
                    d->rec_len = (uint16_t)used;
                }
            }
            if (slot) {
                slot->ino = ino;
                slot->name_len = (uint8_t)nl;
                slot->type = type;
                memcpy(slot->name, name, nl);
                dirblk_seal(b);
                if (xvsx_write_block(x, p, b)) { free(e); return -1; }
                if (fresh) {
                    if (n > 0 && e[n - 1].lblock + e[n - 1].len == l &&
                        e[n - 1].pblock + e[n - 1].len == p)
                        e[n - 1].len++;
                    else
                        e[n++] = (xext_t){(uint32_t)l, 1, p};
                    if (xvsx_ext_store(x, &in, e, n)) { free(e); return -1; }
                    in.size += XVSX_BLOCK_SIZE;
                }
                in.mtime = in.ctime = x->now;
                free(e);
                return xvsx_inode_write(x, dir, &in);
            }
            off += d->rec_len;
        }
    }
    free(e);
    return -1;
}

static uint32_t new_inode_in(xvsx_t *x, uint32_t parent, const char *name,
                             uint16_t mode)
{
    if (xvsx_dir_lookup(x, parent, name))
        return 0;
    return xvsx_alloc_inode(x, mode);
}

uint32_t xvsx_mkdir(xvsx_t *x, uint32_t parent, const char *name, uint16_t mode)
{
    uint32_t ino = parent ? new_inode_in(x, parent, name, XVSX_S_IFDIR | mode)
                          : xvsx_alloc_inode(x, XVSX_S_IFDIR | mode);
    if (!ino)
        return 0;
    if (!parent)
        parent = ino;
    uint32_t blk = xvsx_alloc_block(x);
    if (!blk)
        return 0;
    uint8_t b[XVSX_BLOCK_SIZE];
    dirblk_init(b);
    if (xvsx_write_block(x, blk, b))
        return 0;
    struct xvsx_inode in;
    if (xvsx_inode_read(x, ino, &in))
        return 0;
    xext_t e = {0, 1, blk};
    if (xvsx_ext_store(x, &in, &e, 1))
        return 0;
    in.size = XVSX_BLOCK_SIZE;
    in.nlink = 2;
    if (xvsx_inode_write(x, ino, &in))
        return 0;
    if (xvsx_dir_add(x, ino, ".", ino, XVSX_DT_DIR) ||
        xvsx_dir_add(x, ino, "..", parent, XVSX_DT_DIR))
        return 0;
    if (parent != ino) {
        if (xvsx_dir_add(x, parent, name, ino, XVSX_DT_DIR))
            return 0;
        struct xvsx_inode pin;
        if (xvsx_inode_read(x, parent, &pin))
            return 0;
        pin.nlink++;
        if (xvsx_inode_write(x, parent, &pin))
            return 0;
    }
    return ino;
}

uint32_t xvsx_mkfile(xvsx_t *x, uint32_t parent, const char *name,
                     const void *data, uint64_t len, uint16_t mode)
{
    uint32_t ino = new_inode_in(x, parent, name, XVSX_S_IFREG | mode);
    if (!ino)
        return 0;
    struct xvsx_inode in;
    if (xvsx_inode_read(x, ino, &in))
        return 0;
    in.nlink = 1;
    if (xvsx_inode_write(x, ino, &in) || xvsx_file_write(x, ino, data, len))
        return 0;
    if (xvsx_dir_add(x, parent, name, ino, XVSX_DT_REG))
        return 0;
    return ino;
}

uint32_t xvsx_mksymlink(xvsx_t *x, uint32_t parent, const char *name,
                        const char *target)
{
    uint32_t ino = new_inode_in(x, parent, name, XVSX_S_IFLNK | 0777);
    if (!ino)
        return 0;
    struct xvsx_inode in;
    if (xvsx_inode_read(x, ino, &in))
        return 0;
    in.nlink = 1;
    if (xvsx_inode_write(x, ino, &in) ||
        xvsx_file_write(x, ino, target, strlen(target)))
        return 0;
    if (xvsx_dir_add(x, parent, name, ino, XVSX_DT_LNK))
        return 0;
    return ino;
}

int xvsx_format(xvsx_t *x, const char *path, uint64_t bytes, const char *label)
{
    memset(x, 0, sizeof *x);
    x->rw = 1;
    x->now = now_time();
    uint64_t total = bytes / XVSX_BLOCK_SIZE;
    if (total < 256 || total > 0xFFFFFFF0ull) {
        fprintf(stderr, "xvsx: image size must be 1 MiB .. 16 TiB\n");
        return -1;
    }
    x->fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (x->fd < 0 || ftruncate(x->fd, (off_t)(total * XVSX_BLOCK_SIZE)))
        return -1;

    uint32_t jb = (uint32_t)(total / 32);
    if (jb < 64) jb = 64;
    if (jb > 4096) jb = 4096;
    uint32_t ninodes = (uint32_t)(total / 4);
    if (ninodes < 64) ninodes = 64;
    ninodes = (ninodes + 7) & ~7u;
    struct xvsx_super *sb = &x->sb;
    memcpy(sb->magic, XVSX_MAGIC, 8);
    sb->version_major = XVSX_VERSION_MAJOR;
    sb->version_minor = XVSX_VERSION_MINOR;
    sb->block_size = XVSX_BLOCK_SIZE;
    sb->inode_size = XVSX_INODE_SIZE;
    sb->total_blocks = total;
    sb->total_inodes = ninodes;
    sb->journal_start = 2;
    sb->journal_blocks = jb;
    sb->ibitmap_start = sb->journal_start + jb;
    sb->ibitmap_blocks = (ninodes + XVSX_BITS_PER_BLOCK - 1) / XVSX_BITS_PER_BLOCK;
    sb->bbitmap_start = sb->ibitmap_start + sb->ibitmap_blocks;
    sb->bbitmap_blocks = (uint32_t)((total + XVSX_BITS_PER_BLOCK - 1) / XVSX_BITS_PER_BLOCK);
    sb->itable_start = sb->bbitmap_start + sb->bbitmap_blocks;
    sb->itable_blocks = ninodes / XVSX_INODES_PER_BLOCK;
    sb->data_start = sb->itable_start + sb->itable_blocks;
    if (sb->data_start + 16 >= total) {
        fprintf(stderr, "xvsx: image too small for its own metadata\n");
        return -1;
    }
    sb->root_ino = XVSX_ROOT_INO;
    int ur = open("/dev/urandom", O_RDONLY);
    if (ur >= 0) {
        if (read(ur, sb->uuid, 16) != 16)
            memset(sb->uuid, 0xA5, 16);
        close(ur);
    }
    if (label)
        strncpy(sb->label, label, sizeof sb->label - 1);
    sb->feature_compat = XVSX_COMPAT_JOURNAL;
    sb->feature_incompat = XVSX_INCOMPAT_EXTENTS | XVSX_INCOMPAT_INLINE |
                           XVSX_INCOMPAT_DIRTYPE;
    sb->state = XVSX_STATE_CLEAN;
    sb->mkfs_time = x->now;
    sb->free_blocks = total - sb->data_start;
    sb->free_inodes = ninodes;

    x->ibm = calloc(1, (size_t)sb->ibitmap_blocks * XVSX_BLOCK_SIZE);
    x->bbm = calloc(1, (size_t)sb->bbitmap_blocks * XVSX_BLOCK_SIZE);
    if (!x->ibm || !x->bbm)
        return -1;
    for (uint64_t b = 0; b < sb->data_start; b++)
        xvsx_set_bit(x->bbm, b);
    for (uint64_t b = total; b < (uint64_t)sb->bbitmap_blocks * XVSX_BITS_PER_BLOCK; b++)
        xvsx_set_bit(x->bbm, b);
    for (uint64_t i = ninodes; i < (uint64_t)sb->ibitmap_blocks * XVSX_BITS_PER_BLOCK; i++)
        xvsx_set_bit(x->ibm, i);

    struct xvsx_jsuper js;
    memset(&js, 0, sizeof js);
    memcpy(js.magic, XVSX_JMAGIC, 8);
    js.version = 1;
    js.nblocks = jb;
    js.first = 1;
    js.head = 1;
    js.seq = 1;
    js.state = XVSX_JSTATE_CLEAN;
    js.csum = xvsx_crc32c(0, &js, XVSX_BLOCK_SIZE - 4);
    if (xvsx_write_block(x, sb->journal_start, &js))
        return -1;

    uint32_t root = xvsx_mkdir(x, 0, "", 0755);
    if (root != XVSX_ROOT_INO) {
        fprintf(stderr, "xvsx: could not create the root directory\n");
        return -1;
    }
    return 0;
}
