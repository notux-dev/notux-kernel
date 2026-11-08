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
static void rsect(uint s, void *b) {
    lseek(fd, (off_t)s * BSIZE, 0);
    if (read(fd, b, BSIZE) != BSIZE) { perror("read"); exit(1); }
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <img>\n", argv[0]); return 1; }
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror(argv[1]); return 1; }

    char buf[BSIZE];
    rsect(1, buf);
    struct superblock sb;
    memcpy(&sb, buf, sizeof(sb));
    printf("magic=0x%x size=%u nblocks=%u ninodes=%u nlog=%u logstart=%u "
           "inodestart=%u bmapstart=%u\n",
           sb.magic, sb.size, sb.nblocks, sb.ninodes, sb.nlog, sb.logstart,
           sb.inodestart, sb.bmapstart);
    if (sb.magic != 0x10203040) { printf("BAD MAGIC\n"); return 1; }

    char ibuf[BSIZE];
    rsect(IBLOCK(1, sb), ibuf);
    struct dinode root;
    memcpy(&root, ibuf + (1 % IPB) * sizeof(struct dinode), sizeof(root));
    printf("root inode: type=%d nlink=%d size=%u addrs[0]=%u\n", root.type,
           root.nlink, root.size, root.addrs[0]);

    char dbuf[BSIZE];
    rsect(root.addrs[0], dbuf);
    printf("root directory entries:\n");
    for (uint off = 0; off < root.size && off < BSIZE; off += sizeof(struct dirent)) {
        struct dirent de;
        memcpy(&de, dbuf + off, sizeof(de));
        if (de.inum == 0) continue;
        char name[DIRSIZ + 1] = {0};
        memcpy(name, de.name, DIRSIZ);
        printf("  inum=%-3u name=%s\n", de.inum, name);
        if (strncmp(name, "README", DIRSIZ) == 0) {
            char ibuf2[BSIZE];
            rsect(IBLOCK(de.inum, sb), ibuf2);
            struct dinode rdin;
            memcpy(&rdin, ibuf2 + (de.inum % IPB) * sizeof(struct dinode), sizeof(rdin));
            char rbuf[BSIZE];
            rsect(rdin.addrs[0], rbuf);
            printf("  --- /README (size=%u) ---\n", rdin.size);
            fwrite(rbuf, 1, rdin.size, stdout);
            printf("  --- end ---\n");
        }
    }
    return 0;
}
