#define _GNU_SOURCE
#include "xvsx_lib.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAXEXT 8192
#define MAX_REPORT 60

static xvsx_t X;
static int errors, verbose;
static uint8_t *claimed;
static uint8_t *visited;
static uint32_t *refs;
static uint32_t *nlinks;

static void err(const char *fmt, ...)
{
    errors++;
    if (errors > MAX_REPORT) {
        if (errors == MAX_REPORT + 1)
            fprintf(stderr, "fsck: ... further messages suppressed\n");
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "fsck: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static int claim(uint64_t blk, uint32_t ino)
{
    if (blk < X.sb.data_start || blk >= X.sb.total_blocks) {
        err("inode %u: block %llu outside the data area", ino, (unsigned long long)blk);
        return -1;
    }
    if (xvsx_test_bit(claimed, blk)) {
        err("inode %u: block %llu is claimed more than once", ino, (unsigned long long)blk);
        return -1;
    }
    xvsx_set_bit(claimed, blk);
    return 0;
}

static int check_node(uint32_t ino, const uint8_t *buf, int is_root, int level,
                      int want_depth, xext_t *out, int *n, uint64_t *nodes)
{
    const struct xvsx_ext_header *h = (const void *)buf;
    int ok = 1;
    if (h->magic != XVSX_EXT_MAGIC) {
        err("inode %u: bad extent header magic", ino);
        return -1;
    }
    int max = is_root ? XVSX_EXT_ROOT_MAX : XVSX_EXT_NODE_MAX;
    if (h->max != max || h->entries > h->max) {
        err("inode %u: extent header entries/max invalid (%u/%u)", ino, h->entries, h->max);
        return -1;
    }
    if (!is_root && h->depth != want_depth) {
        err("inode %u: extent node depth %u, expected %d", ino, h->depth, want_depth);
        return -1;
    }
    if (h->depth > XVSX_EXT_MAX_DEPTH || level > XVSX_EXT_MAX_DEPTH) {
        err("inode %u: extent tree too deep", ino);
        return -1;
    }
    if (!is_root) {
        uint32_t c = *(const uint32_t *)(buf + XVSX_EXT_NODE_CSUM_OFF);
        if (c != xvsx_crc32c(0, buf, XVSX_EXT_NODE_CSUM_OFF)) {
            err("inode %u: extent node checksum mismatch", ino);
            ok = 0;
        }
    }
    const struct xvsx_ext *e = (const void *)(buf + sizeof *h);
    uint32_t prev_l = 0;
    for (int i = 0; i < h->entries; i++) {
        if (i && e[i].lblock < prev_l)
            err("inode %u: extent entries not in ascending order", ino);
        prev_l = e[i].lblock;
        if (h->depth == 0) {
            if (e[i].len == 0) {
                err("inode %u: zero-length extent", ino);
                continue;
            }
            if (*n >= MAXEXT) { err("inode %u: too many extents", ino); return -1; }
            out[(*n)++] = (xext_t){e[i].lblock, e[i].len, e[i].pblock};
        } else {
            uint32_t cb = e[i].len;
            if (claim(cb, ino))
                continue;
            (*nodes)++;
            uint8_t child[XVSX_BLOCK_SIZE];
            if (xvsx_read_block(&X, cb, child)) { err("inode %u: cannot read node %u", ino, cb); continue; }
            check_node(ino, child, 0, level + 1, h->depth - 1, out, n, nodes);
        }
    }
    return ok ? 0 : -1;
}

typedef struct { char name[XVSX_NAME_MAX + 1]; } nm_t;
static int nm_cmp(const void *a, const void *b) { return strcmp(((const nm_t *)a)->name, ((const nm_t *)b)->name); }

static void check_inode(uint32_t ino, uint32_t parent, int from_dirent_type);

static void check_dir(uint32_t ino, uint32_t parent, struct xvsx_inode *in,
                      xext_t *e, int n)
{
    uint64_t nb = in->size / XVSX_BLOCK_SIZE;
    if (in->size % XVSX_BLOCK_SIZE || nb == 0)
        err("dir %u: size %llu is not a positive multiple of the block size", ino,
            (unsigned long long)in->size);
    nm_t *names = NULL;
    size_t nnames = 0, capn = 0;
    int seen_dot = 0, seen_dotdot = 0;
    for (uint64_t l = 0; l < nb; l++) {
        uint32_t p = xvsx_ext_map(e, n, (uint32_t)l);
        uint8_t b[XVSX_BLOCK_SIZE];
        if (!p) { err("dir %u: logical block %llu is a hole", ino, (unsigned long long)l); continue; }
        if (xvsx_read_block(&X, p, b)) { err("dir %u: cannot read block %u", ino, p); continue; }
        struct xvsx_dirent *t = (void *)(b + XVSX_DIR_TAIL_OFF);
        if (t->ino || t->rec_len != 16 || t->name_len || t->type != XVSX_DT_TAIL)
            err("dir %u: block %u has a malformed tail record", ino, p);
        if (*(uint32_t *)(b + 4092) != xvsx_crc32c(0, b, 4092))
            err("dir %u: block %u checksum mismatch", ino, p);
        uint32_t off = 0;
        int first = 1;
        while (off < XVSX_DIR_FIRST_LEN) {
            struct xvsx_dirent *d = (void *)(b + off);
            if (d->rec_len < 8 || d->rec_len % 8 || off + d->rec_len > XVSX_DIR_FIRST_LEN ||
                (d->ino && d->rec_len < XVSX_DIRENT_LEN(d->name_len))) {
                err("dir %u: block %u offset %u: bad record length %u", ino, p, off, d->rec_len);
                break;
            }
            if (d->ino) {
                if (!d->name_len) { err("dir %u: empty name", ino); }
                char nm[XVSX_NAME_MAX + 1];
                memcpy(nm, d->name, d->name_len);
                nm[d->name_len] = 0;
                if (memchr(d->name, '/', d->name_len) || strlen(nm) != d->name_len)
                    err("dir %u: illegal character in a name", ino);
                if (nnames == capn) {
                    capn = capn ? capn * 2 : 64;
                    names = realloc(names, capn * sizeof *names);
                }
                strcpy(names[nnames++].name, nm);
                int isdot = !strcmp(nm, "."), isdd = !strcmp(nm, "..");
                if (l == 0 && first && !isdot)
                    err("dir %u: first entry is not \".\"", ino);
                if (isdot) {
                    seen_dot = 1;
                    if (d->ino != ino) err("dir %u: \".\" points to %u", ino, d->ino);
                    refs[ino]++;
                } else if (isdd) {
                    seen_dotdot = 1;
                    if (d->ino != parent)
                        err("dir %u: \"..\" points to %u, expected %u", ino, d->ino, parent);
                    if (d->ino >= 1 && d->ino <= X.sb.total_inodes)
                        refs[d->ino]++;
                } else {
                    if (d->ino < 1 || d->ino > X.sb.total_inodes) {
                        err("dir %u: entry \"%s\" has invalid inode %u", ino, nm, d->ino);
                    } else {
                        refs[d->ino]++;
                        check_inode(d->ino, ino, d->type);
                    }
                }
                first = 0;
            }
            off += d->rec_len;
        }
        if (off != XVSX_DIR_FIRST_LEN)
            err("dir %u: block %u records do not fill the block exactly", ino, p);
    }
    if (!seen_dot || !seen_dotdot)
        err("dir %u: missing \".\" or \"..\"", ino);
    if (nnames > 1) {
        qsort(names, nnames, sizeof *names, nm_cmp);
        for (size_t i = 1; i < nnames; i++)
            if (!strcmp(names[i].name, names[i - 1].name))
                err("dir %u: duplicate name \"%s\"", ino, names[i].name);
    }
    free(names);
}

static void check_inode(uint32_t ino, uint32_t parent, int dt)
{
    if (ino < 1 || ino > X.sb.total_inodes)
        return;
    if (!xvsx_test_bit(X.ibm, ino - 1))
        err("inode %u is referenced but marked free in the inode bitmap", ino);
    struct xvsx_inode in;
    int r = xvsx_inode_read(&X, ino, &in);
    if (r == -1) { err("inode %u: cannot read", ino); return; }
    uint16_t type = in.mode & XVSX_S_IFMT;
    if (dt != -1 && dt != XVSX_DT_UNKNOWN && xvsx_mode_to_dt(in.mode) != dt)
        err("inode %u: directory entry type %d does not match mode %06o", ino, dt, in.mode);
    if (visited[ino]) {
        if (type == XVSX_S_IFDIR)
            err("directory inode %u has more than one parent entry", ino);
        return;
    }
    visited[ino] = 1;
    nlinks[ino] = in.nlink;
    if (r == -2)
        err("inode %u: checksum mismatch", ino);
    if (type != XVSX_S_IFREG && type != XVSX_S_IFDIR && type != XVSX_S_IFLNK &&
        type != XVSX_S_IFCHR && type != XVSX_S_IFBLK && type != XVSX_S_IFIFO) {
        err("inode %u: invalid file type in mode %06o", ino, in.mode);
        return;
    }
    if (in.flags & ~(XVSX_IF_EXTENTS | XVSX_IF_INLINE))
        err("inode %u: unknown flag bits %#x", ino, in.flags);
    for (int i = 0; i < (int)sizeof in.pad0; i++)
        if (in.pad0[i]) { err("inode %u: reserved bytes are not zero", ino); break; }
    for (int i = 0; i < (int)sizeof in.xattr; i++)
        if (in.xattr[i]) { err("inode %u: xattr area is not zero (unsupported in v1)", ino); break; }
    if (in.nlink == 0)
        err("inode %u: link count is 0 but it is reachable", ino);

    xext_t *e = malloc(sizeof(xext_t) * MAXEXT);
    int n = 0;
    uint64_t nodes = 0, data_blocks = 0;
    if ((in.flags & XVSX_IF_INLINE) && (in.flags & XVSX_IF_EXTENTS))
        err("inode %u: both inline and extents flags set", ino);
    if (in.flags & XVSX_IF_INLINE) {
        if (type != XVSX_S_IFREG && type != XVSX_S_IFLNK)
            err("inode %u: inline data on a non-file", ino);
        if (in.size > XVSX_INLINE_MAX)
            err("inode %u: inline size %llu > %d", ino, (unsigned long long)in.size, XVSX_INLINE_MAX);
        if (in.blocks)
            err("inode %u: inline file owns %llu blocks", ino, (unsigned long long)in.blocks);
    } else if (in.flags & XVSX_IF_EXTENTS) {
        check_node(ino, in.data, 1, 0, 0, e, &n, &nodes);
        uint64_t end = 0;
        for (int i = 0; i < n; i++) {
            if (e[i].lblock < end)
                err("inode %u: extents overlap or are unordered at logical %u", ino, e[i].lblock);
            end = (uint64_t)e[i].lblock + e[i].len;
            for (uint32_t j = 0; j < e[i].len; j++)
                if (claim((uint64_t)e[i].pblock + j, ino))
                    break;
            data_blocks += e[i].len;
        }
        if (in.blocks != data_blocks + nodes)
            err("inode %u: blocks field %llu, actual %llu", ino,
                (unsigned long long)in.blocks, (unsigned long long)(data_blocks + nodes));
        if (in.size > end * XVSX_BLOCK_SIZE)
            err("inode %u: size %llu exceeds mapped space", ino, (unsigned long long)in.size);
    } else {
        if (in.size && type != XVSX_S_IFDIR)
            err("inode %u: has size %llu but no data mapping", ino, (unsigned long long)in.size);
        if (type == XVSX_S_IFDIR)
            err("directory inode %u has no blocks", ino);
    }
    if (type == XVSX_S_IFDIR && (in.flags & XVSX_IF_EXTENTS))
        check_dir(ino, parent, &in, e, n);
    if (type == XVSX_S_IFLNK && in.size == 0)
        err("inode %u: empty symlink", ino);
    free(e);
    if (verbose)
        printf("checked inode %u (mode %06o, %llu bytes)\n", ino, in.mode,
               (unsigned long long)in.size);
}

static void check_super_geometry(void)
{
    struct xvsx_super *s = &X.sb;
    struct stat st;
    if (fstat(X.fd, &st) == 0 && (uint64_t)st.st_size < s->total_blocks * XVSX_BLOCK_SIZE)
        err("image file (%lld bytes) is smaller than the filesystem (%llu bytes)",
            (long long)st.st_size, (unsigned long long)(s->total_blocks * XVSX_BLOCK_SIZE));
    if (s->root_ino != XVSX_ROOT_INO) err("root inode number is %u", s->root_ino);
    if (s->journal_start < 2) err("journal overlaps the superblock");
    if (s->ibitmap_start < s->journal_start + s->journal_blocks) err("inode bitmap overlaps the journal");
    if (s->bbitmap_start < s->ibitmap_start + s->ibitmap_blocks) err("block bitmap overlaps the inode bitmap");
    if (s->itable_start < s->bbitmap_start + s->bbitmap_blocks) err("inode table overlaps the bitmaps");
    if (s->data_start < s->itable_start + s->itable_blocks) err("data area overlaps the inode table");
    if (s->data_start >= s->total_blocks) err("data area starts beyond the end of the filesystem");
    if ((uint64_t)s->ibitmap_blocks * XVSX_BITS_PER_BLOCK < s->total_inodes) err("inode bitmap too small");
    if ((uint64_t)s->bbitmap_blocks * XVSX_BITS_PER_BLOCK < s->total_blocks) err("block bitmap too small");
    if ((uint64_t)s->itable_blocks * XVSX_INODES_PER_BLOCK < s->total_inodes) err("inode table too small");
    for (int i = 0; i < (int)sizeof s->reserved; i++)
        if (s->reserved[i]) { err("superblock reserved area is not zero"); break; }
    if (s->state != XVSX_STATE_CLEAN) err("filesystem was not cleanly unmounted");
    if (s->feature_incompat & XVSX_INCOMPAT_RECOVER) err("journal needs recovery (not supported by fsck)");

    struct xvsx_jsuper js;
    if (xvsx_read_block(&X, s->journal_start, &js)) { err("cannot read the journal superblock"); return; }
    if (memcmp(js.magic, XVSX_JMAGIC, 8)) err("journal superblock has bad magic");
    else {
        if (js.csum != xvsx_crc32c(0, &js, XVSX_BLOCK_SIZE - 4)) err("journal superblock checksum mismatch");
        if (js.nblocks != s->journal_blocks) err("journal size mismatch (%u vs %u)", js.nblocks, s->journal_blocks);
        if (js.state != XVSX_JSTATE_CLEAN) err("journal is not clean (needs replay)");
    }
}

int main(int argc, char **argv)
{
    const char *img = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else img = argv[i];
    }
    if (!img) {
        fprintf(stderr, "usage: %s [-v] IMAGE\n", argv[0]);
        return 2;
    }
    int r = xvsx_open(&X, img, 0);
    if (r) {
        static const char *why[] = {"", "not an xvsXfs image (bad magic)", "superblock checksum mismatch",
                                    "unsupported version", "unsupported block/inode size",
                                    "unknown incompatible features"};
        fprintf(stderr, "fsck: %s: %s\n", img, r < 0 && -r < 6 ? why[-r] : "cannot open");
        return 2;
    }
    struct xvsx_super *s = &X.sb;
    check_super_geometry();
    if (errors) {
        fprintf(stderr, "fsck: superblock problems, not checking further\n");
        return 1;
    }
    claimed = calloc(1, (size_t)s->bbitmap_blocks * XVSX_BLOCK_SIZE);
    visited = calloc(s->total_inodes + 2, 1);
    refs = calloc(s->total_inodes + 2, sizeof *refs);
    nlinks = calloc(s->total_inodes + 2, sizeof *nlinks);

    for (uint64_t b = s->total_blocks; b < (uint64_t)s->bbitmap_blocks * XVSX_BITS_PER_BLOCK; b++)
        if (!xvsx_test_bit(X.bbm, b)) { err("block bitmap padding bit %llu is clear", (unsigned long long)b); break; }
    for (uint64_t i = s->total_inodes; i < (uint64_t)s->ibitmap_blocks * XVSX_BITS_PER_BLOCK; i++)
        if (!xvsx_test_bit(X.ibm, i)) { err("inode bitmap padding bit %llu is clear", (unsigned long long)i); break; }

    check_inode(XVSX_ROOT_INO, XVSX_ROOT_INO, XVSX_DT_DIR);

    uint32_t nvisited = 0;
    for (uint32_t i = 1; i <= s->total_inodes; i++) {
        int used = xvsx_test_bit(X.ibm, i - 1);
        if (visited[i]) {
            nvisited++;
            if (refs[i] != nlinks[i])
                err("inode %u: link count %u but %u directory entries refer to it", i, nlinks[i], refs[i]);
        } else if (used) {
            err("inode %u is allocated but not reachable from the root (orphan)", i);
        }
    }

    uint64_t leak = 0, missing = 0, free_count = 0;
    for (uint64_t b = 0; b < s->total_blocks; b++) {
        int marked = xvsx_test_bit(X.bbm, b);
        int expected = b < s->data_start || xvsx_test_bit(claimed, b);
        if (!marked) free_count++;
        if (marked && !expected) { if (++leak <= 5) err("block %llu is marked used but nothing references it", (unsigned long long)b); }
        if (!marked && expected) { if (++missing <= 5) err("block %llu is in use but marked free", (unsigned long long)b); }
    }
    if (leak > 5) err("... %llu blocks marked used but unreferenced in total", (unsigned long long)leak);
    if (missing > 5) err("... %llu blocks in use but marked free in total", (unsigned long long)missing);
    if (free_count != s->free_blocks)
        err("superblock says %llu free blocks, bitmap has %llu", (unsigned long long)s->free_blocks,
            (unsigned long long)free_count);
    uint32_t free_inodes = 0;
    for (uint32_t i = 0; i < s->total_inodes; i++)
        if (!xvsx_test_bit(X.ibm, i)) free_inodes++;
    if (free_inodes != s->free_inodes)
        err("superblock says %u free inodes, bitmap has %u", s->free_inodes, free_inodes);

    if (errors) {
        fprintf(stderr, "fsck: %s: %d problem(s) found\n", img, errors);
        return 1;
    }
    printf("fsck: %s: clean (%u inodes in use, %llu blocks in use of %llu)\n", img, nvisited,
           (unsigned long long)(s->total_blocks - free_count), (unsigned long long)s->total_blocks);
    return 0;
}
