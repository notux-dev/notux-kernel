
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

typedef uint32_t uint;
typedef uint16_t ushort;

/* ---- mirrors kernel/Xjail/xv6/fs.h exactly ---- */
#define BSIZE 512
#define XV6_MAGIC 0x10203040
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)
#define ROOTINO 1
#define DIRSIZ 14

struct superblock {
    uint magic;
    uint size;
    uint nblocks;
    uint ninodes;
    uint nlog;
    uint logstart;
    uint inodestart;
    uint bmapstart;
};

struct dinode {
    int16_t type;
    int16_t major;
    int16_t minor;
    int16_t nlink;
    uint32_t size;
    uint32_t addrs[NDIRECT + 2];
};

#define IPB (BSIZE / sizeof(struct dinode))
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart)
#define BPB (BSIZE * 8)
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)

struct dirent {
    uint16_t inum;
    char name[DIRSIZ];
};

/* ---- mirrors kernel/Xjail/xv6/file.h ---- */
#define T_DIR  1
#define T_FILE 2

/* ---- tunable image parameters ---- */
#define FSSIZE   8192   /* total blocks -> 4 MiB image */
#define NINODES  200
#define LOGSIZE  30     /* reserved, unused by the current bridge --
                          * see the big comment above */

static int fsfd;
static struct superblock sb;
static uint freeblock;
static uint freeinode = 1;

static void wsect(uint sec, void *buf)
{
    if (lseek(fsfd, (off_t)sec * BSIZE, 0) != (off_t)sec * BSIZE) {
        perror("lseek");
        exit(1);
    }
    if (write(fsfd, buf, BSIZE) != BSIZE) {
        perror("write");
        exit(1);
    }
}

static void rsect(uint sec, void *buf)
{
    if (lseek(fsfd, (off_t)sec * BSIZE, 0) != (off_t)sec * BSIZE) {
        perror("lseek");
        exit(1);
    }
    if (read(fsfd, buf, BSIZE) != BSIZE) {
        perror("read");
        exit(1);
    }
}

static void winode(uint inum, struct dinode *ip)
{
    char buf[BSIZE];
    uint bn = IBLOCK(inum, sb);
    rsect(bn, buf);
    struct dinode *dip = ((struct dinode *)buf) + (inum % IPB);
    *dip = *ip;
    wsect(bn, buf);
}

static void rinode(uint inum, struct dinode *ip)
{
    char buf[BSIZE];
    uint bn = IBLOCK(inum, sb);
    rsect(bn, buf);
    struct dinode *dip = ((struct dinode *)buf) + (inum % IPB);
    *ip = *dip;
}

static uint ialloc(ushort type)
{
    uint inum = freeinode++;
    struct dinode din;
    memset(&din, 0, sizeof(din));
    din.type = (int16_t)type;
    din.nlink = 1;
    din.size = 0;
    winode(inum, &din);
    return inum;
}

static void balloc_mark_used(uint used)
{
    /* Classic xv6 approach: allocate data blocks sequentially starting
     * right after the bitmap region, then write ONE bitmap pass at the
     * very end marking [0, used) as allocated and the rest free. */
    char buf[BSIZE];
    memset(buf, 0, BSIZE);
    for (uint i = 0; i < used; i++) {
        buf[i / 8] |= (0x1 << (i % 8));
    }
    for (uint b = sb.bmapstart; b < sb.bmapstart + 2; b++) {
        memset(buf, 0, BSIZE);
        for (uint bi = 0; bi < BPB; bi++) {
            uint blockno = (b - sb.bmapstart) * BPB + bi;
            if (blockno < used) {
                buf[bi / 8] |= (0x1 << (bi % 8));
            }
        }
        wsect(b, buf);
    }
}

/* Append `n` bytes to inode `inum`'s content, following direct, then
 * singly-indirect, then doubly-indirect blocks -- exactly mirroring
 * kernel/xv6fs/fs.c's bmap() so files written here read back correctly
 * at runtime no matter their size. */
static void iappend(uint inum, void *xp, int n)
{
    char *p = (char *)xp;
    struct dinode din;
    rinode(inum, &din);
    uint off = din.size;
    uint indirect[NINDIRECT];
    uint dindirect[NINDIRECT];

    while (n > 0) {
        uint fbn = off / BSIZE;
        assert(fbn < MAXFILE);
        uint x;
        if (fbn < NDIRECT) {
            if (din.addrs[fbn] == 0) {
                din.addrs[fbn] = freeblock++;
            }
            x = din.addrs[fbn];
        } else if (fbn < NDIRECT + NINDIRECT) {
            uint bn = fbn - NDIRECT;
            if (din.addrs[NDIRECT] == 0) {
                din.addrs[NDIRECT] = freeblock++;
            }
            rsect(din.addrs[NDIRECT], (char *)indirect);
            if (indirect[bn] == 0) {
                indirect[bn] = freeblock++;
                wsect(din.addrs[NDIRECT], (char *)indirect);
            }
            x = indirect[bn];
        } else {
            uint bn2 = fbn - NDIRECT - NINDIRECT;
            uint idx1 = bn2 / NINDIRECT;
            uint idx2 = bn2 % NINDIRECT;
            if (din.addrs[NDIRECT + 1] == 0) {
                din.addrs[NDIRECT + 1] = freeblock++;
            }
            rsect(din.addrs[NDIRECT + 1], (char *)dindirect);
            if (dindirect[idx1] == 0) {
                dindirect[idx1] = freeblock++;
                wsect(din.addrs[NDIRECT + 1], (char *)dindirect);
            }
            uint dblock[NINDIRECT];
            rsect(dindirect[idx1], (char *)dblock);
            if (dblock[idx2] == 0) {
                dblock[idx2] = freeblock++;
                wsect(dindirect[idx1], (char *)dblock);
            }
            x = dblock[idx2];
        }
        uint n1 = (n < (int)(BSIZE - off % BSIZE))
                      ? n
                      : (int)(BSIZE - off % BSIZE);
        char buf[BSIZE];
        rsect(x, buf);
        memmove(buf + off % BSIZE, p, n1);
        wsect(x, buf);
        n -= n1;
        off += n1;
        p += n1;
    }
    din.size = off;
    winode(inum, &din);
}

static void dirlink(uint dir_inum, const char *name, uint target_inum)
{
    struct dirent de;
    memset(&de, 0, sizeof(de));
    de.inum = (uint16_t)target_inum;
    strncpy(de.name, name, DIRSIZ);
    iappend(dir_inum, &de, sizeof(de));
}


static uint make_dir_under_root(uint root_inum, const char *name)
{
    uint inum = ialloc(T_DIR);
    dirlink(inum, ".", inum);
    dirlink(inum, "..", root_inum);
    dirlink(root_inum, name, inum);
    struct dinode din;
    rinode(inum, &din);
    din.nlink = 2; /* "." plus the entry in the parent */
    winode(inum, &din);
    return inum;
}

int main(int argc, char *argv[])
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr,
                "usage: %s <image-out> [binary-to-embed-at-/bin/child]\n",
                argv[0]);
        return 1;
    }

    fsfd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fsfd < 0) {
        perror(argv[1]);
        return 1;
    }

    uint nmeta, nbitmap, ninodeblocks;
    ninodeblocks = (NINODES / IPB) + 1;
    nbitmap = (FSSIZE / (BSIZE * 8)) + 1;
    nmeta = 2 + LOGSIZE + ninodeblocks + nbitmap; /* boot + super + ... */

    sb.magic = XV6_MAGIC;
    sb.size = FSSIZE;
    sb.nblocks = FSSIZE - nmeta;
    sb.ninodes = NINODES;
    sb.nlog = LOGSIZE;
    sb.logstart = 2;
    sb.inodestart = 2 + LOGSIZE;
    sb.bmapstart = 2 + LOGSIZE + ninodeblocks;

    printf("mkfs: size %u nmeta %u (boot 1, super 1, log %u, inode "
           "%u, bitmap %u) blocks %u\n",
           sb.size, nmeta, LOGSIZE, ninodeblocks, nbitmap, sb.nblocks);

    freeblock = nmeta; /* first data block right after all metadata */

    /* zero the whole image up front */
    char zbuf[BSIZE];
    memset(zbuf, 0, BSIZE);
    for (uint i = 0; i < FSSIZE; i++) {
        wsect(i, zbuf);
    }

    /* superblock */
    char sbuf[BSIZE];
    memset(sbuf, 0, BSIZE);
    memmove(sbuf, &sb, sizeof(sb));
    wsect(1, sbuf);

    /* root directory */
    uint root_inum = ialloc(T_DIR);
    assert(root_inum == ROOTINO);
    dirlink(root_inum, ".", root_inum);
    dirlink(root_inum, "..", root_inum);

    uint etc_inum = make_dir_under_root(root_inum, "etc");
    uint bin_inum = make_dir_under_root(root_inum, "bin");
    uint roothome_inum = make_dir_under_root(root_inum, "root");
    (void)etc_inum;
    (void)roothome_inum;

    /* root's own nlink: "." + entries from etc/bin/root's ".." do NOT
     * point back here beyond what make_dir_under_root already linked
     * (each subdir's ".." counts as one link to root), plus root's own
     * "." -- start at 2 (".", and being referenced by nothing above it
     * since it has no parent) and add one per child directory. */
    {
        struct dinode din;
        rinode(root_inum, &din);
        din.nlink = 2 + 3; /* "." + ".." (self) + etc + bin + root */
        winode(root_inum, &din);
    }

    static const char readme_text[] =
        "Welcome to notux.\n"
        "\n"
        "This file was written by tools/mkfs.c when this disk image\n"
        "was built -- if you can read this, the AHCI driver and\n"
        "xv6fs are both working end to end.\n"
        "\n"
        "Layout:\n"
        "  /bin   executables\n"
        "  /etc   configuration\n"
        "  /root  root's home directory\n";
    uint readme_inum = ialloc(T_FILE);
    iappend(readme_inum, (void *)readme_text, (int)strlen(readme_text));
    dirlink(root_inum, "README", readme_inum);

    if (argc == 3) {
        FILE *bf = fopen(argv[2], "rb");
        if (!bf) {
            perror(argv[2]);
            return 1;
        }
        char fbuf[4096];
        uint child_inum = ialloc(T_FILE);
        size_t got;
        while ((got = fread(fbuf, 1, sizeof(fbuf), bf)) > 0) {
            iappend(child_inum, fbuf, (int)got);
        }
        fclose(bf);
        dirlink(bin_inum, "child", child_inum);
        printf("mkfs: embedded %s as /bin/child\n", argv[2]);
    }

    uint used = freeblock;
    balloc_mark_used(used);

    printf("mkfs: wrote %s (%u blocks used of %u)\n", argv[1], used,
           sb.size);

    close(fsfd);
    return 0;
}
