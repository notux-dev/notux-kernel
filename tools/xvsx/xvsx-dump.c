#define _GNU_SOURCE
#include "xvsx_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *typename_(uint8_t t)
{
    switch (t) {
    case XVSX_DT_DIR: return "d"; case XVSX_DT_REG: return "-";
    case XVSX_DT_LNK: return "l"; case XVSX_DT_CHR: return "c";
    default: return "?";
    }
}

static int ls_cb(void *ctx, uint32_t ino, uint8_t t, const char *nm, int nl)
{
    xvsx_t *x = ctx;
    struct xvsx_inode in;
    int bad = xvsx_inode_read(x, ino, &in);
    printf("%s %4o %3u %10llu  ino=%-6u %.*s%s\n", typename_(t),
           in.mode & 07777, in.nlink, (unsigned long long)in.size, ino, nl, nm,
           bad ? "   [BAD INODE CHECKSUM]" : "");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s IMAGE super|ls [/p]|cat /p|stat /p\n", argv[0]);
        return 2;
    }
    xvsx_t x;
    int r = xvsx_open(&x, argv[1], 0);
    if (r) {
        fprintf(stderr, "%s: cannot open as xvsXfs (code %d)\n", argv[1], r);
        return 1;
    }
    const char *cmd = argv[2];
    const char *path = argc > 3 ? argv[3] : "/";
    if (!strcmp(cmd, "super")) {
        struct xvsx_super *s = &x.sb;
        printf("version      %u.%u\nlabel        %.32s\nblocks       %llu (%llu free)\n"
               "inodes       %u (%u free)\njournal      block %u, %u blocks\n"
               "inode bitmap block %u (+%u)\nblock bitmap block %u (+%u)\n"
               "inode table  block %u (+%u)\ndata starts  block %u\nstate        %s\n"
               "features     compat=%#x incompat=%#x ro=%#x\n",
               s->version_major, s->version_minor, s->label,
               (unsigned long long)s->total_blocks, (unsigned long long)s->free_blocks,
               s->total_inodes, s->free_inodes, s->journal_start, s->journal_blocks,
               s->ibitmap_start, s->ibitmap_blocks, s->bbitmap_start, s->bbitmap_blocks,
               s->itable_start, s->itable_blocks, s->data_start,
               s->state == XVSX_STATE_CLEAN ? "clean" : "NOT CLEAN",
               s->feature_compat, s->feature_incompat, s->feature_ro_compat);
    } else {
        uint32_t ino = xvsx_lookup(&x, path);
        if (!ino) {
            fprintf(stderr, "%s: not found\n", path);
            return 1;
        }
        struct xvsx_inode in;
        if (xvsx_inode_read(&x, ino, &in))
            fprintf(stderr, "warning: inode %u checksum mismatch\n", ino);
        if (!strcmp(cmd, "ls")) {
            if ((in.mode & XVSX_S_IFMT) == XVSX_S_IFDIR)
                xvsx_dir_walk(&x, ino, ls_cb, &x);
            else
                ls_cb(&x, ino, xvsx_mode_to_dt(in.mode), path, (int)strlen(path));
        } else if (!strcmp(cmd, "cat")) {
            void *buf;
            int64_t n = xvsx_file_read(&x, ino, &buf);
            if (n < 0) { fprintf(stderr, "read failed\n"); return 1; }
            fwrite(buf, 1, (size_t)n, stdout);
            free(buf);
        } else if (!strcmp(cmd, "stat")) {
            xext_t e[8192];
            int n = xvsx_ext_load(&x, &in, e, 8192);
            printf("inode %u  mode %06o  nlink %u  size %llu  blocks %llu\n"
                   "uid %u gid %u  flags %#x%s%s  mtime %lld\n", ino, in.mode,
                   in.nlink, (unsigned long long)in.size,
                   (unsigned long long)in.blocks, in.uid, in.gid, in.flags,
                   in.flags & XVSX_IF_INLINE ? " (inline)" : "",
                   in.flags & XVSX_IF_EXTENTS ? " (extents)" : "",
                   (long long)in.mtime);
            for (int i = 0; i < n && i < 16; i++)
                printf("  extent: logical %u  len %u  physical %u\n", e[i].lblock,
                       e[i].len, e[i].pblock);
            if (n > 16)
                printf("  ... %d extents total\n", n);
        } else {
            fprintf(stderr, "unknown command %s\n", cmd);
            return 2;
        }
    }
    xvsx_close(&x);
    return 0;
}
