#include "../Xjail/xv6/types.h"
#include "../Xjail/xv6/defs.h"
#include "../Xjail/xv6/param.h"
#include "../Xjail/xv6/spinlock.h"
#include "../Xjail/xv6/sleeplock.h"
#include "../Xjail/xv6/fs.h"
#include "../Xjail/xv6/file.h"
#include "ext2.h"
#include "console.h"
#include "ktime.h"

struct inode *iget(uint dev, uint inum);
void iput(struct inode *ip);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);

#define E2_MAGIC 0xEF53
#define E2_EXIST 17
#define E2_NOTDIR 20
#define E2_ISDIR 21
#define E2_NOSPC 28
#define E2_NOTEMPTY 39
#define E2_INVAL 22
#define E2_NAMETOOLONG 36
#define E2_MLINK 31
#define INCOMPAT_FILETYPE 0x0002
#define INCOMPAT_SUPPORTED (INCOMPAT_FILETYPE)

struct e2_sb {
    uint32_t inodes_count, blocks_count, r_blocks_count, free_blocks, free_inodes;
    uint32_t first_data_block, log_block_size, log_frag_size;
    uint32_t blocks_per_group, frags_per_group, inodes_per_group;
    uint32_t mtime, wtime;
    uint16_t mnt_count, max_mnt_count, magic, state, errors, minor_rev;
    uint32_t lastcheck, checkinterval, creator_os, rev_level;
    uint16_t def_resuid, def_resgid;
    uint32_t first_ino;
    uint16_t inode_size, block_group_nr;
    uint32_t feature_compat, feature_incompat, feature_ro_compat;
} __attribute__((packed));

struct e2_gd {
    uint32_t block_bitmap, inode_bitmap, inode_table;
    uint16_t free_blocks, free_inodes, used_dirs, pad;
    uint32_t reserved[3];
} __attribute__((packed));

struct e2_inode {
    uint16_t mode, uid;
    uint32_t size, atime, ctime, mtime, dtime;
    uint16_t gid, links;
    uint32_t blocks, flags, osd1;
    uint32_t block[15];
    uint32_t generation, file_acl, size_high, faddr;
    uint8_t  osd2[12];
} __attribute__((packed));

struct e2_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len, file_type;
    char     name[];
} __attribute__((packed));

static ahci_device_t *dev_;
static struct e2_sb sb_;
static uint32_t bsize_, ngroups_, inode_size_;
static int mounted_;
static uint8_t sb_raw_[1024];
static uint mnt_inum_;

#define NCACHE 16
#define MAXBS  4096
static struct { uint32_t blk; int valid; uint8_t data[MAXBS]; } cache_[NCACHE];
static int victim_;

static const uint8_t *getblk(uint32_t blk)
{
    for (int i = 0; i < NCACHE; i++)
        if (cache_[i].valid && cache_[i].blk == blk)
            return cache_[i].data;
    int s = victim_; victim_ = (victim_ + 1) % NCACHE;
    cache_[s].valid = 0;
    if (ahci_read_sectors(dev_, (uint64_t)blk * (bsize_ / 512), bsize_ / 512, cache_[s].data) != 0)
        return 0;
    cache_[s].blk = blk; cache_[s].valid = 1;
    return cache_[s].data;
}

int ext2_is_mounted(void) { return mounted_; }
void ext2_set_mountpoint(uint n) { mnt_inum_ = n; }
int ext2_mountpoint(void) { return mounted_ ? (int)mnt_inum_ : 0; }
static int root_mode_;
void ext2_set_root_mode(int on) { root_mode_ = on; }
int ext2_is_root(void) { return mounted_ && root_mode_; }
static uint bind_ext2_, bind_mp_;
void ext2_set_bind(uint ext2_ino, uint xv6_mp) { bind_ext2_ = ext2_ino; bind_mp_ = xv6_mp; }
uint ext2_bind_ino(void) { return mounted_ ? bind_ext2_ : 0; }
uint ext2_bind_mp(void) { return bind_mp_; }

int ext2_mount(ahci_device_t *dev)
{
    uint8_t raw[1024];
    dev_ = dev; mounted_ = 0;
    if (ahci_read_sectors(dev, 2, 2, raw) != 0) return -1;
    memmove(&sb_, raw, sizeof(sb_));
    memmove(sb_raw_, raw, 1024);
    if (sb_.magic != E2_MAGIC) return -1;
    if (sb_.log_block_size > 2) return -1;
    bsize_ = 1024u << sb_.log_block_size;
    if (sb_.rev_level >= 1 && (sb_.feature_incompat & ~INCOMPAT_SUPPORTED)) {
        kprint("ext2: unsupported incompat features (journal recovery/extents/...)\n", 0x00FF0000);
        return -1;
    }
    inode_size_ = sb_.rev_level >= 1 ? sb_.inode_size : 128;
    if (inode_size_ < 128 || inode_size_ > bsize_ || sb_.inodes_per_group == 0) return -1;
    ngroups_ = (sb_.blocks_count - sb_.first_data_block + sb_.blocks_per_group - 1) / sb_.blocks_per_group;
    for (int i = 0; i < NCACHE; i++) cache_[i].valid = 0;
    mounted_ = 1;
    return 0;
}

static int read_disk_inode(uint32_t ino, struct e2_inode *out)
{
    if (ino == 0 || ino > sb_.inodes_count) return -1;
    uint32_t g = (ino - 1) / sb_.inodes_per_group, idx = (ino - 1) % sb_.inodes_per_group;
    uint32_t gdt_blk = sb_.first_data_block + 1;
    uint32_t per = bsize_ / sizeof(struct e2_gd);
    const uint8_t *gb = getblk(gdt_blk + g / per);
    if (!gb) return -1;
    struct e2_gd gd; memmove(&gd, gb + (g % per) * sizeof(gd), sizeof(gd));
    uint32_t off = idx * inode_size_;
    const uint8_t *ib = getblk(gd.inode_table + off / bsize_);
    if (!ib) return -1;
    memmove(out, ib + off % bsize_, sizeof(*out));
    return 0;
}

int ext2_load(struct inode *ip)
{
    struct e2_inode d;
    if (!mounted_ || read_disk_inode(ip->inum, &d) != 0) return -1;
    uint16_t fmt = d.mode & 0xF000;
    ip->mode = d.mode; ip->uid = d.uid; ip->gid = d.gid; ip->mtime = d.mtime;
    ip->atime = d.atime; ip->ctime = d.ctime;
    ip->nlink = (short)d.links;
    ip->size = d.size;
    ip->major = ip->minor = 0;
    switch (fmt) {
    case 0x4000: ip->type = T_DIR; break;
    case 0x8000: ip->type = T_FILE; break;
    case 0xA000: ip->type = T_LNK; break;
    case 0x2000: case 0x6000: case 0x1000: case 0xC000: {
        ip->type = T_DEV;
        uint32_t b0 = d.block[0], b1 = d.block[1];
        if (b0) { ip->major = (b0 >> 8) & 0xff; ip->minor = b0 & 0xff; }
        else    { ip->major = (b1 & 0xfff00) >> 8; ip->minor = (b1 & 0xff) | ((b1 >> 12) & 0xfff00); }
        break; }
    default: ip->type = T_FILE;
    }
    for (int i = 0; i < 15; i++) ip->e2blk[i] = d.block[i];
    ip->e2iblocks = d.blocks;
    ip->e2fast = (fmt == 0xA000 && d.blocks == 0 && d.size < 60);
    return 0;
}

static uint32_t e2_bmap(struct inode *ip, uint32_t lbn)
{
    uint32_t apb = bsize_ / 4;
    if (lbn < 12) return ip->e2blk[lbn];
    lbn -= 12;
    uint32_t b; const uint8_t *p;
    if (lbn < apb) {
        if (!(b = ip->e2blk[12]) || !(p = getblk(b))) return 0;
        return ((const uint32_t *)p)[lbn];
    }
    lbn -= apb;
    if (lbn < apb * apb) {
        if (!(b = ip->e2blk[13]) || !(p = getblk(b))) return 0;
        b = ((const uint32_t *)p)[lbn / apb];
        if (!b || !(p = getblk(b))) return 0;
        return ((const uint32_t *)p)[lbn % apb];
    }
    lbn -= apb * apb;
    if (!(b = ip->e2blk[14]) || !(p = getblk(b))) return 0;
    b = ((const uint32_t *)p)[lbn / (apb * apb)];
    if (!b || !(p = getblk(b))) return 0;
    b = ((const uint32_t *)p)[(lbn / apb) % apb];
    if (!b || !(p = getblk(b))) return 0;
    return ((const uint32_t *)p)[lbn % apb];
}

int ext2_readi(struct inode *ip, char *dst, uint off, uint n)
{
    if (off > ip->size || off + n < off) return -1;
    if (off + n > ip->size) n = ip->size - off;
    uint tot = 0;
    while (tot < n) {
        uint m = bsize_ - off % bsize_;
        if (m > n - tot) m = n - tot;
        uint32_t pb = e2_bmap(ip, off / bsize_);
        if (pb == 0) memset(dst, 0, m);
        else {
            const uint8_t *p = getblk(pb);
            if (!p) return tot ? (int)tot : -1;
            memmove(dst, p + off % bsize_, m);
        }
        tot += m; off += m; dst += m;
    }
    return (int)n;
}

int ext2_readdir(struct inode *dp, uint *pos, char *name, int *namelen, uint *ino, int *dtype)
{
    uint8_t hdr[8];
    while (*pos + 8 <= dp->size) {
        if (ext2_readi(dp, (char *)hdr, *pos, 8) != 8) return 0;
        struct e2_dirent *de = (struct e2_dirent *)hdr;
        if (de->rec_len < 8 || (de->rec_len & 3)) return 0;
        uint cur = *pos; *pos += de->rec_len;
        if (de->inode == 0) continue;
        int nl = de->name_len;
        if (ext2_readi(dp, name, cur + 8, (uint)nl) != nl) return 0;
        name[nl] = 0; *namelen = nl; *ino = de->inode;
        static const uint8_t ft[8] = {0, 8, 4, 2, 6, 1, 12, 10};
        *dtype = de->file_type < 8 ? ft[de->file_type] : 0;
        return 1;
    }
    return 0;
}

struct inode *ext2_dirlookup(struct inode *dp, const char *name, int len)
{
    char nm[256]; int nl, dt; uint pos = 0, ino;
    while (ext2_readdir(dp, &pos, nm, &nl, &ino, &dt)) {
        if (nl == len && strncmp(nm, name, (uint)len) == 0)
            return iget(EXT2_DEV, ino);
    }
    return 0;
}

int ext2_readlink(struct inode *ip, char *buf, int max)
{
    int n = (int)ip->size < max ? (int)ip->size : max;
    if (ip->e2fast) { memmove(buf, (char *)ip->e2blk, (uint)n); return n; }
    return ext2_readi(ip, buf, 0, (uint)n);
}

void ext2_statfs(uint *blocks, uint *bfree, uint *files, uint *ffree, uint *bsize)
{
    *blocks = sb_.blocks_count; *bfree = sb_.free_blocks;
    *files = sb_.inodes_count; *ffree = sb_.free_inodes; *bsize = bsize_;
}


static uint32_t now_(void) { return (uint32_t)(ktime_realtime_ns() / 1000000000ull); }

static int flushblk(uint32_t blk)
{
    for (int i = 0; i < NCACHE; i++)
        if (cache_[i].valid && cache_[i].blk == blk)
            return ahci_write_sectors(dev_, (uint64_t)blk * (bsize_ / 512), bsize_ / 512, cache_[i].data);
    return -1;
}
static uint8_t *getblk_w(uint32_t blk) { return (uint8_t *)getblk(blk); }

static int newblk(uint32_t blk)
{
    int s = -1;
    for (int i = 0; i < NCACHE; i++)
        if (cache_[i].valid && cache_[i].blk == blk) { s = i; break; }
    if (s < 0) { s = victim_; victim_ = (victim_ + 1) % NCACHE; }
    memset(cache_[s].data, 0, bsize_);
    cache_[s].blk = blk; cache_[s].valid = 1;
    return flushblk(blk);
}

static int sb_put(void)
{
    uint8_t *raw = sb_raw_;
    memmove(raw + 12, &sb_.free_blocks, 4);
    memmove(raw + 16, &sb_.free_inodes, 4);
    return ahci_write_sectors(dev_, 2, 2, raw);
}
static int gd_get(uint32_t g, struct e2_gd *gd)
{
    uint32_t per = bsize_ / sizeof(struct e2_gd);
    const uint8_t *p = getblk(sb_.first_data_block + 1 + g / per);
    if (!p) return -1;
    memmove(gd, p + (g % per) * sizeof(*gd), sizeof(*gd));
    return 0;
}
static int gd_put(uint32_t g, const struct e2_gd *gd)
{
    uint32_t per = bsize_ / sizeof(struct e2_gd), blk = sb_.first_data_block + 1 + g / per;
    uint8_t *p = getblk_w(blk);
    if (!p) return -1;
    memmove(p + (g % per) * sizeof(*gd), gd, sizeof(*gd));
    return flushblk(blk);
}

static uint32_t alloc_block(uint32_t pref)
{
    for (uint32_t n = 0; n < ngroups_; n++) {
        uint32_t g = (pref + n) % ngroups_;
        struct e2_gd gd;
        if (gd_get(g, &gd) != 0 || gd.free_blocks == 0) continue;
        uint32_t nb = sb_.blocks_count - sb_.first_data_block - g * sb_.blocks_per_group;
        if (nb > sb_.blocks_per_group) nb = sb_.blocks_per_group;
        uint8_t *bm = getblk_w(gd.block_bitmap);
        if (!bm) continue;
        for (uint32_t i = 0; i < nb; i++) {
            if (bm[i >> 3] & (1 << (i & 7))) continue;
            bm[i >> 3] |= (uint8_t)(1 << (i & 7));
            if (flushblk(gd.block_bitmap) != 0) return 0;
            gd.free_blocks--; gd_put(g, &gd);
            sb_.free_blocks--; sb_put();
            uint32_t blk = sb_.first_data_block + g * sb_.blocks_per_group + i;
            if (newblk(blk) != 0) return 0;
            return blk;
        }
    }
    return 0;
}
static void free_block(uint32_t blk)
{
    if (blk < sb_.first_data_block || blk >= sb_.blocks_count) return;
    uint32_t g = (blk - sb_.first_data_block) / sb_.blocks_per_group;
    uint32_t i = (blk - sb_.first_data_block) % sb_.blocks_per_group;
    struct e2_gd gd;
    if (gd_get(g, &gd) != 0) return;
    uint8_t *bm = getblk_w(gd.block_bitmap);
    if (!bm || !(bm[i >> 3] & (1 << (i & 7)))) return;
    bm[i >> 3] &= (uint8_t)~(1 << (i & 7));
    flushblk(gd.block_bitmap);
    gd.free_blocks++; gd_put(g, &gd);
    sb_.free_blocks++; sb_put();
    for (int k = 0; k < NCACHE; k++)
        if (cache_[k].valid && cache_[k].blk == blk) cache_[k].valid = 0;
}

static uint8_t *inode_loc(uint32_t ino, uint32_t *blkout)
{
    uint32_t g = (ino - 1) / sb_.inodes_per_group, idx = (ino - 1) % sb_.inodes_per_group;
    struct e2_gd gd;
    if (gd_get(g, &gd) != 0) return 0;
    uint32_t off = idx * inode_size_, blk = gd.inode_table + off / bsize_;
    uint8_t *p = getblk_w(blk);
    if (!p) return 0;
    *blkout = blk;
    return p + off % bsize_;
}
static uint32_t alloc_inode(uint32_t pref, int isdir)
{
    uint32_t first = sb_.rev_level >= 1 ? sb_.first_ino : 11;
    for (uint32_t n = 0; n < ngroups_; n++) {
        uint32_t g = (pref + n) % ngroups_;
        struct e2_gd gd;
        if (gd_get(g, &gd) != 0 || gd.free_inodes == 0) continue;
        uint8_t *bm = getblk_w(gd.inode_bitmap);
        if (!bm) continue;
        for (uint32_t i = 0; i < sb_.inodes_per_group; i++) {
            uint32_t ino = g * sb_.inodes_per_group + i + 1;
            if (ino < first || (bm[i >> 3] & (1 << (i & 7)))) continue;
            bm[i >> 3] |= (uint8_t)(1 << (i & 7));
            if (flushblk(gd.inode_bitmap) != 0) return 0;
            gd.free_inodes--; if (isdir) gd.used_dirs++;
            gd_put(g, &gd);
            sb_.free_inodes--; sb_put();
            return ino;
        }
    }
    return 0;
}
static void free_inode(uint32_t ino, int isdir)
{
    uint32_t g = (ino - 1) / sb_.inodes_per_group, i = (ino - 1) % sb_.inodes_per_group;
    struct e2_gd gd;
    if (gd_get(g, &gd) != 0) return;
    uint8_t *bm = getblk_w(gd.inode_bitmap);
    if (!bm) return;
    bm[i >> 3] &= (uint8_t)~(1 << (i & 7));
    flushblk(gd.inode_bitmap);
    gd.free_inodes++; if (isdir && gd.used_dirs) gd.used_dirs--;
    gd_put(g, &gd);
    sb_.free_inodes++; sb_put();
}

int ext2_iupdate(struct inode *ip)
{
    uint32_t blk;
    uint8_t *p = inode_loc(ip->inum, &blk);
    if (!p) return -1;
    struct e2_inode *d = (struct e2_inode *)p;
    d->mode = (uint16_t)ip->mode; d->uid = (uint16_t)ip->uid; d->gid = (uint16_t)ip->gid;
    d->size = ip->size; d->atime = ip->atime; d->ctime = ip->ctime; d->mtime = ip->mtime;
    d->links = (uint16_t)ip->nlink; d->blocks = ip->e2iblocks;
    for (int i = 0; i < 15; i++) d->block[i] = ip->e2blk[i];
    if (ip->type == T_FILE) d->size_high = 0;
    return flushblk(blk);
}

static uint32_t grp_of(struct inode *ip) { return (ip->inum - 1) / sb_.inodes_per_group; }

static uint32_t walk(struct inode *ip, uint32_t *slot, const uint32_t *idx, int depth, int create)
{
    uint32_t b = *slot;
    if (!b) {
        if (!create) return 0;
        b = alloc_block(grp_of(ip));
        if (!b) return 0;
        *slot = b; ip->e2iblocks += bsize_ / 512;
    }
    for (int d = 0; d < depth; d++) {
        const uint32_t *tbl = (const uint32_t *)getblk(b);
        if (!tbl) return 0;
        uint32_t nb = tbl[idx[d]];
        if (!nb) {
            if (!create) return 0;
            nb = alloc_block(grp_of(ip));
            if (!nb) return 0;
            ip->e2iblocks += bsize_ / 512;
            uint32_t *t2 = (uint32_t *)getblk_w(b);
            if (!t2) return 0;
            t2[idx[d]] = nb;
            flushblk(b);
        }
        b = nb;
    }
    return b;
}
static uint32_t bmap_w(struct inode *ip, uint32_t lbn, int create)
{
    uint32_t apb = bsize_ / 4, idx[3];
    if (lbn < 12) return walk(ip, &ip->e2blk[lbn], idx, 0, create);
    lbn -= 12;
    if (lbn < apb) { idx[0] = lbn; return walk(ip, &ip->e2blk[12], idx, 1, create); }
    lbn -= apb;
    if (lbn < apb * apb) { idx[0] = lbn / apb; idx[1] = lbn % apb; return walk(ip, &ip->e2blk[13], idx, 2, create); }
    lbn -= apb * apb;
    idx[0] = lbn / (apb * apb); idx[1] = (lbn / apb) % apb; idx[2] = lbn % apb;
    if (idx[0] >= apb) return 0;
    return walk(ip, &ip->e2blk[14], idx, 3, create);
}

int ext2_writei(struct inode *ip, const char *src, uint off, uint n)
{
    if (!mounted_ || ip->type == T_DIR || off + n < off) return -1;
    uint tot = 0;
    while (tot < n) {
        uint32_t lbn = off / bsize_, bo = off % bsize_;
        uint m = bsize_ - bo;
        if (m > n - tot) m = n - tot;
        uint32_t pb = bmap_w(ip, lbn, 1);
        if (!pb) break;
        uint8_t *p = getblk_w(pb);
        if (!p) break;
        memmove(p + bo, src, m);
        if (flushblk(pb) != 0) break;
        tot += m; off += m; src += m;
        if (off > ip->size) ip->size = off;
    }
    if (tot) { ip->mtime = ip->ctime = now_(); ext2_iupdate(ip); }
    return tot ? (int)tot : -E2_NOSPC;
}

static int trunc_sub(struct inode *ip, uint32_t *slot, int depth, uint64_t base, uint64_t first)
{
    uint32_t b = *slot;
    if (!b) return 1;
    uint64_t apb = bsize_ / 4, span = 1;
    for (int i = 0; i < depth; i++) span *= apb;
    if (base + span <= first) return 0;
    if (depth == 0) {
        free_block(b); ip->e2iblocks -= bsize_ / 512; *slot = 0;
        return 1;
    }
    uint64_t child = span / apb;
    int empty = 1;
    for (uint32_t i = 0; i < apb; i++) {
        const uint32_t *t = (const uint32_t *)getblk(b);
        if (!t) return 0;
        uint32_t cb = t[i];
        if (!cb) continue;
        uint64_t cbase = base + (uint64_t)i * child;
        if (cbase + child <= first) { empty = 0; continue; }
        uint32_t tmp = cb;
        int e = trunc_sub(ip, &tmp, depth - 1, cbase, first);
        if (e) {
            uint32_t *w = (uint32_t *)getblk_w(b);
            if (!w) return 0;
            w[i] = 0; flushblk(b);
        } else empty = 0;
    }
    if (empty && base >= first) {
        free_block(b); ip->e2iblocks -= bsize_ / 512; *slot = 0;
        return 1;
    }
    return 0;
}

static void free_all_blocks(struct inode *ip, uint64_t first)
{
    uint64_t apb = bsize_ / 4;
    for (int i = 0; i < 12; i++) trunc_sub(ip, &ip->e2blk[i], 0, (uint64_t)i, first);
    trunc_sub(ip, &ip->e2blk[12], 1, 12, first);
    trunc_sub(ip, &ip->e2blk[13], 2, 12 + apb, first);
    trunc_sub(ip, &ip->e2blk[14], 3, 12 + apb + apb * apb, first);
}

int ext2_truncate(struct inode *ip, uint len)
{
    if (!mounted_) return -5;
    if (ip->type != T_FILE) return -E2_INVAL;
    if (len < ip->size) {
        uint64_t first = ((uint64_t)len + bsize_ - 1) / bsize_;
        free_all_blocks(ip, first);
        if (len % bsize_) {
            uint32_t pb = bmap_w(ip, len / bsize_, 0);
            if (pb) {
                uint8_t *p = getblk_w(pb);
                if (p) { memset(p + len % bsize_, 0, bsize_ - len % bsize_); flushblk(pb); }
            }
        }
    }
    ip->size = len;
    ip->mtime = ip->ctime = now_();
    return ext2_iupdate(ip) == 0 ? 0 : -5;
}

void ext2_evict(struct inode *ip)
{
    if (!mounted_) return;
    int isdir = ip->type == T_DIR;
    if (!ip->e2fast && ip->type != T_DEV) free_all_blocks(ip, 0);
    ip->size = 0; ip->nlink = 0; ip->e2iblocks = 0;
    ip->ctime = now_();
    uint32_t blk;
    uint8_t *p = inode_loc(ip->inum, &blk);
    if (p) {
        struct e2_inode *d = (struct e2_inode *)p;
        d->dtime = now_();
        for (int i = 0; i < 15; i++) ip->e2blk[i] = 0;
        ext2_iupdate(ip);
    }
    free_inode(ip->inum, isdir);
}

static uint align4_(uint x) { return (x + 3) & ~3u; }

static uint32_t dir_find(struct inode *dp, const char *name, int len, uint32_t *pblk, uint *poff, uint *pprev)
{
    uint nblk = (dp->size + bsize_ - 1) / bsize_;
    for (uint lb = 0; lb < nblk; lb++) {
        uint32_t pb = bmap_w(dp, lb, 0);
        if (!pb) continue;
        const uint8_t *p = getblk(pb);
        if (!p) return 0;
        uint off = 0, prev = (uint)-1;
        while (off + 8 <= bsize_) {
            const struct e2_dirent *de = (const struct e2_dirent *)(p + off);
            if (de->rec_len < 8 || off + de->rec_len > bsize_) break;
            if (de->inode && de->name_len == len && strncmp(de->name, name, (uint)len) == 0) {
                if (pblk) *pblk = pb;
                if (poff) *poff = off;
                if (pprev) *pprev = prev;
                return de->inode;
            }
            prev = off; off += de->rec_len;
        }
    }
    return 0;
}

static int dir_add(struct inode *dp, const char *name, int len, uint32_t ino, int ftype)
{
    uint need = align4_(8 + (uint)len);
    uint nblk = (dp->size + bsize_ - 1) / bsize_;
    for (uint lb = 0; lb < nblk; lb++) {
        uint32_t pb = bmap_w(dp, lb, 0);
        if (!pb) continue;
        uint8_t *p = getblk_w(pb);
        if (!p) return -5;
        uint off = 0;
        while (off + 8 <= bsize_) {
            struct e2_dirent *de = (struct e2_dirent *)(p + off);
            if (de->rec_len < 8 || off + de->rec_len > bsize_) break;
            uint used = de->inode ? align4_(8 + de->name_len) : 0;
            if (de->rec_len - used >= need) {
                uint rl = de->rec_len;
                struct e2_dirent *ne = de;
                if (used) {
                    de->rec_len = (uint16_t)used;
                    ne = (struct e2_dirent *)(p + off + used);
                    ne->rec_len = (uint16_t)(rl - used);
                }
                ne->inode = ino; ne->name_len = (uint8_t)len; ne->file_type = (uint8_t)ftype;
                memmove(ne->name, name, (uint)len);
                flushblk(pb);
                dp->mtime = dp->ctime = now_(); ext2_iupdate(dp);
                return 0;
            }
            off += de->rec_len;
        }
    }
    uint32_t pb = bmap_w(dp, nblk, 1);
    if (!pb) return -E2_NOSPC;
    uint8_t *p = getblk_w(pb);
    if (!p) return -5;
    struct e2_dirent *ne = (struct e2_dirent *)p;
    ne->inode = ino; ne->rec_len = (uint16_t)bsize_; ne->name_len = (uint8_t)len; ne->file_type = (uint8_t)ftype;
    memmove(ne->name, name, (uint)len);
    flushblk(pb);
    dp->size += bsize_;
    dp->mtime = dp->ctime = now_(); ext2_iupdate(dp);
    return 0;
}

static int dir_remove(struct inode *dp, const char *name, int len)
{
    uint32_t pb; uint off, prev;
    if (!dir_find(dp, name, len, &pb, &off, &prev)) return -2;
    uint8_t *p = getblk_w(pb);
    if (!p) return -5;
    struct e2_dirent *de = (struct e2_dirent *)(p + off);
    if (prev != (uint)-1) ((struct e2_dirent *)(p + prev))->rec_len += de->rec_len;
    else de->inode = 0;
    flushblk(pb);
    dp->mtime = dp->ctime = now_(); ext2_iupdate(dp);
    return 0;
}

static int dir_is_empty(struct inode *dp)
{
    char nm[256]; int nl, dt; uint pos = 0, ino;
    while (ext2_readdir(dp, &pos, nm, &nl, &ino, &dt))
        if (!((nl == 1 && nm[0] == '.') || (nl == 2 && nm[0] == '.' && nm[1] == '.'))) return 0;
    return 1;
}

static int ftype_of(uint mode)
{
    switch (mode & 0xF000) {
    case 0x8000: return 1; case 0x4000: return 2; case 0x2000: return 3;
    case 0x6000: return 4; case 0x1000: return 5; case 0xC000: return 6; case 0xA000: return 7;
    }
    return 0;
}

static struct inode *new_inode(struct inode *dp, uint mode)
{
    uint32_t ino = alloc_inode(grp_of(dp), (mode & 0xF000) == 0x4000);
    if (!ino) return 0;
    uint32_t blk;
    uint8_t *p = inode_loc(ino, &blk);
    if (!p) { free_inode(ino, 0); return 0; }
    memset(p, 0, inode_size_);
    struct e2_inode *d = (struct e2_inode *)p;
    uint32_t t = now_();
    d->mode = (uint16_t)mode; d->links = 1;
    d->atime = d->ctime = d->mtime = t;
    flushblk(blk);
    struct inode *ip = iget(EXT2_DEV, ino);
    ip->valid = 0;
    if (ext2_load(ip) != 0) { iput(ip); free_inode(ino, 0); return 0; }
    ip->valid = 1;
    return ip;
}

int ext2_create(struct inode *dp, const char *name, int len, uint mode, struct inode **out)
{
    if (len <= 0 || len > 255) return -E2_NAMETOOLONG;
    if (dir_find(dp, name, len, 0, 0, 0)) return -E2_EXIST;
    struct inode *ip = new_inode(dp, 0x8000 | (mode & 07777));
    if (!ip) return -E2_NOSPC;
    int r = dir_add(dp, name, len, ip->inum, 1);
    if (r < 0) { ip->nlink = 0; iput(ip); return r; }
    *out = ip;
    return 0;
}

int ext2_mkdir(struct inode *dp, const char *name, int len, uint mode, struct inode **out)
{
    if (len <= 0 || len > 255) return -E2_NAMETOOLONG;
    if (dir_find(dp, name, len, 0, 0, 0)) return -E2_EXIST;
    if (dp->nlink >= 32000) return -E2_MLINK;
    struct inode *ip = new_inode(dp, 0x4000 | (mode & 07777));
    if (!ip) return -E2_NOSPC;
    uint32_t pb = bmap_w(ip, 0, 1);
    if (!pb) { ip->nlink = 0; iput(ip); return -E2_NOSPC; }
    uint8_t *p = getblk_w(pb);
    struct e2_dirent *d1 = (struct e2_dirent *)p;
    d1->inode = ip->inum; d1->rec_len = 12; d1->name_len = 1; d1->file_type = 2; d1->name[0] = '.';
    struct e2_dirent *d2 = (struct e2_dirent *)(p + 12);
    d2->inode = dp->inum; d2->rec_len = (uint16_t)(bsize_ - 12); d2->name_len = 2; d2->file_type = 2;
    d2->name[0] = '.'; d2->name[1] = '.';
    flushblk(pb);
    ip->size = bsize_; ip->nlink = 2;
    ext2_iupdate(ip);
    int r = dir_add(dp, name, len, ip->inum, 2);
    if (r < 0) { ip->nlink = 0; iput(ip); return r; }
    dp->nlink++; ext2_iupdate(dp);
    *out = ip;
    return 0;
}

int ext2_symlink(struct inode *dp, const char *name, int len, const char *target, int tlen)
{
    if (len <= 0 || len > 255 || tlen <= 0 || tlen >= (int)bsize_) return -E2_NAMETOOLONG;
    if (dir_find(dp, name, len, 0, 0, 0)) return -E2_EXIST;
    struct inode *ip = new_inode(dp, 0xA000 | 0777);
    if (!ip) return -E2_NOSPC;
    if (tlen < 60) {
        memmove((char *)ip->e2blk, target, (uint)tlen);
        ip->e2fast = 1;
        ip->size = (uint)tlen;
        ext2_iupdate(ip);
    } else {
        ip->size = 0;
        int w = ext2_writei(ip, target, 0, (uint)tlen);
        if (w != tlen) { ip->nlink = 0; iput(ip); return -E2_NOSPC; }
    }
    int r = dir_add(dp, name, len, ip->inum, 7);
    if (r < 0) ip->nlink = 0;
    iput(ip);
    return r;
}

int ext2_unlink(struct inode *dp, const char *name, int len, int want)
{
    uint32_t ino = dir_find(dp, name, len, 0, 0, 0);
    if (!ino) return -2;
    struct inode *ip = iget(EXT2_DEV, ino);
    ilock(ip);
    int isdir = ip->type == T_DIR, r = 0;
    if (isdir && want == 0) r = -E2_ISDIR;
    else if (!isdir && want == 1) r = -E2_NOTDIR;
    else if (isdir && !dir_is_empty(ip)) r = -E2_NOTEMPTY;
    if (r == 0) {
        r = dir_remove(dp, name, len);
        if (r == 0) {
            if (isdir) { ip->nlink = 0; dp->nlink--; ext2_iupdate(dp); }
            else if (ip->nlink > 0) ip->nlink--;
            ip->ctime = now_();
            ext2_iupdate(ip);
        }
    }
    iunlock(ip);
    iput(ip);
    return r;
}

int ext2_link(struct inode *dp, const char *name, int len, struct inode *ip)
{
    if (len <= 0 || len > 255) return -E2_NAMETOOLONG;
    if (ip->type == T_DIR) return -1;
    if (ip->nlink >= 32000) return -E2_MLINK;
    if (dir_find(dp, name, len, 0, 0, 0)) return -E2_EXIST;
    int r = dir_add(dp, name, len, ip->inum, ftype_of(ip->mode));
    if (r < 0) return r;
    ip->nlink++; ip->ctime = now_();
    ext2_iupdate(ip);
    return 0;
}

static uint32_t parent_of(uint32_t ino)
{
    struct e2_inode d;
    if (read_disk_inode(ino, &d) != 0 || d.block[0] == 0) return 0;
    const uint8_t *p = getblk(d.block[0]);
    if (!p) return 0;
    const struct e2_dirent *de = (const struct e2_dirent *)p;
    if (de->rec_len < 8 || de->rec_len + 8 > bsize_) return 0;
    return ((const struct e2_dirent *)(p + de->rec_len))->inode;
}

static int is_ancestor(struct inode *anc, struct inode *d)
{
    uint32_t cur = d->inum;
    for (int i = 0; i < 64; i++) {
        if (cur == anc->inum) return 1;
        if (cur == EXT2_ROOT_INO) return 0;
        uint32_t pn = parent_of(cur);
        if (!pn || pn == cur) return 0;
        cur = pn;
    }
    return 1;
}

int ext2_rename(struct inode *odp, const char *on, int ol, struct inode *ndp, const char *nn, int nl)
{
    if (nl <= 0 || nl > 255) return -E2_NAMETOOLONG;
    uint32_t sino = dir_find(odp, on, ol, 0, 0, 0);
    if (!sino) return -2;
    uint32_t dino = dir_find(ndp, nn, nl, 0, 0, 0);
    if (dino == sino) return 0;
    if (sino == ndp->inum) return -E2_INVAL;
    if (dino && (dino == odp->inum || dino == ndp->inum)) return -E2_NOTEMPTY;
    struct inode *src = iget(EXT2_DEV, sino);
    ilock(src);
    int sdir = src->type == T_DIR, r = 0;
    if (sdir && is_ancestor(src, ndp)) r = -E2_INVAL;
    if (r == 0 && dino) {
        struct inode *dst = iget(EXT2_DEV, dino);
        ilock(dst);
        int ddir = dst->type == T_DIR;
        if (sdir && !ddir) r = -E2_NOTDIR;
        else if (!sdir && ddir) r = -E2_ISDIR;
        else if (ddir && !dir_is_empty(dst)) r = -E2_NOTEMPTY;
        if (r == 0) {
            r = dir_remove(ndp, nn, nl);
            if (r == 0) {
                if (ddir) { dst->nlink = 0; ndp->nlink--; ext2_iupdate(ndp); }
                else if (dst->nlink > 0) dst->nlink--;
                ext2_iupdate(dst);
            }
        }
        iunlock(dst); iput(dst);
    }
    if (r == 0) r = dir_add(ndp, nn, nl, sino, ftype_of(src->mode));
    if (r == 0) {
        r = dir_remove(odp, on, ol);
        if (r == 0 && sdir && odp != ndp) {
            uint32_t pb; uint off, prev;
            if (dir_find(src, "..", 2, &pb, &off, &prev)) {
                uint8_t *p = getblk_w(pb);
                if (p) { ((struct e2_dirent *)(p + off))->inode = ndp->inum; flushblk(pb); }
            }
            odp->nlink--; ndp->nlink++;
            ext2_iupdate(odp); ext2_iupdate(ndp);
        }
        src->ctime = now_(); ext2_iupdate(src);
    }
    iunlock(src); iput(src);
    return r;
}

int ext2_setattr(struct inode *ip, int64_t mode, int64_t uid, int64_t gid, int64_t atime, int64_t mtime)
{
    if (mode >= 0) ip->mode = (ip->mode & 0xF000) | ((uint)mode & 07777);
    if (uid >= 0) ip->uid = (uint)uid & 0xffff;
    if (gid >= 0) ip->gid = (uint)gid & 0xffff;
    if (atime >= 0) ip->atime = (uint)atime;
    if (mtime >= 0) ip->mtime = (uint)mtime;
    ip->ctime = now_();
    return ext2_iupdate(ip) == 0 ? 0 : -5;
}
