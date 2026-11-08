#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

typedef uint32_t uint;
#define BSIZE 512
#define NDIRECT 12
#define DIRSIZ 14
#define T_DIR 1
#define T_FILE 2

struct superblock {
    uint magic, size, nblocks, ninodes, nlog, logstart, inodestart, bmapstart;
};
struct dinode {
    int16_t type, major, minor, nlink;
    uint32_t size;
    uint32_t addrs[NDIRECT + 2];
};
struct dirent {
    uint16_t inum;
    char name[DIRSIZ];
};
#define IPB (BSIZE / sizeof(struct dinode))
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart)

static int fd;
static struct superblock sb;

static void rsect(uint s, void *b) {
    lseek(fd, (off_t)s * BSIZE, 0);
    if (read(fd, b, BSIZE) != BSIZE) { perror("read"); exit(1); }
}

static void read_inode(uint inum, struct dinode *out) {
    char ibuf[BSIZE];
    rsect(IBLOCK(inum, sb), ibuf);
    memcpy(out, ibuf + (inum % IPB) * sizeof(struct dinode), sizeof(*out));
}

static uint block_for(struct dinode *din, uint bn) {
    if (bn < NDIRECT) return din->addrs[bn];
    bn -= NDIRECT;
    char ibuf[BSIZE];
    rsect(din->addrs[NDIRECT], ibuf);
    uint *ind = (uint *)ibuf;
    return ind[bn];
}

static uint16_t dir_lookup(struct dinode *dirin, const char *name) {
    for (uint off = 0; off < dirin->size; off += sizeof(struct dirent)) {
        uint bn = off / BSIZE;
        uint boff = off % BSIZE;
        char dbuf[BSIZE];
        rsect(block_for(dirin, bn), dbuf);
        struct dirent de;
        memcpy(&de, dbuf + boff, sizeof(de));
        if (de.inum == 0) continue;
        char nm[DIRSIZ + 1] = {0};
        memcpy(nm, de.name, DIRSIZ);
        if (strncmp(nm, name, DIRSIZ) == 0) return de.inum;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <img> </path/to/file>\n", argv[0]); return 1; }
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror(argv[1]); return 1; }

    char buf[BSIZE];
    rsect(1, buf);
    memcpy(&sb, buf, sizeof(sb));
    if (sb.magic != 0x10203040) { printf("BAD MAGIC\n"); return 1; }

    struct dinode cur;
    read_inode(1, &cur);

    char path[256];
    strncpy(path, argv[2], sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;

    char *p = path;
    if (*p == '/') p++;
    char *tok;
    uint16_t inum = 1;
    while ((tok = strsep(&p, "/")) != NULL) {
        if (*tok == 0) continue;
        inum = dir_lookup(&cur, tok);
        if (inum == 0) {
            fprintf(stderr, "not found: %s (component '%s')\n", argv[2], tok);
            return 1;
        }
        read_inode(inum, &cur);
    }

    if (cur.type != T_FILE) {
        fprintf(stderr, "not a regular file (type=%d)\n", cur.type);
        return 1;
    }

    uint remaining = cur.size;
    uint bn = 0;
    while (remaining > 0) {
        char dbuf[BSIZE];
        rsect(block_for(&cur, bn), dbuf);
        uint n = remaining < BSIZE ? remaining : BSIZE;
        fwrite(dbuf, 1, n, stdout);
        remaining -= n;
        bn++;
    }
    return 0;
}
