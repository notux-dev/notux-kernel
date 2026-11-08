#ifndef XVSX_FORMAT_H
#define XVSX_FORMAT_H
#include <stdint.h>

#define XVSX_BLOCK_SIZE   4096
#define XVSX_INODE_SIZE   512
#define XVSX_INODES_PER_BLOCK (XVSX_BLOCK_SIZE / XVSX_INODE_SIZE)
#define XVSX_BITS_PER_BLOCK   (XVSX_BLOCK_SIZE * 8)

#define XVSX_MAGIC        "XVSXFS01"
#define XVSX_VERSION_MAJOR 1
#define XVSX_VERSION_MINOR 0

#define XVSX_SB_BLOCK     1
#define XVSX_ROOT_INO     1
#define XVSX_NAME_MAX     255

#define XVSX_INCOMPAT_EXTENTS   0x0001
#define XVSX_INCOMPAT_INLINE    0x0002
#define XVSX_INCOMPAT_DIRTYPE   0x0004
#define XVSX_INCOMPAT_RECOVER   0x0008
#define XVSX_INCOMPAT_KNOWN     0x000F
#define XVSX_ROCOMPAT_KNOWN     0x0000
#define XVSX_COMPAT_JOURNAL     0x0001

#define XVSX_STATE_CLEAN  1
#define XVSX_STATE_DIRTY  2

struct xvsx_super {
    char     magic[8];
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t block_size;
    uint32_t inode_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t journal_start;
    uint32_t journal_blocks;
    uint32_t ibitmap_start;
    uint32_t ibitmap_blocks;
    uint32_t bbitmap_start;
    uint32_t bbitmap_blocks;
    uint32_t itable_start;
    uint32_t itable_blocks;
    uint32_t data_start;
    uint32_t root_ino;
    uint8_t  uuid[16];
    char     label[32];
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint32_t state;
    int64_t  mkfs_time;
    int64_t  mount_time;
    uint32_t mount_count;
    uint32_t pad0;
    uint8_t  reserved[3912];
    uint32_t pad1;
    uint32_t csum;
} __attribute__((packed));
_Static_assert(sizeof(struct xvsx_super) == XVSX_BLOCK_SIZE, "superblock size");

#define XVSX_S_IFMT   0170000
#define XVSX_S_IFREG  0100000
#define XVSX_S_IFDIR  0040000
#define XVSX_S_IFLNK  0120000
#define XVSX_S_IFCHR  0020000
#define XVSX_S_IFBLK  0060000
#define XVSX_S_IFIFO  0010000

#define XVSX_IF_EXTENTS 0x0001
#define XVSX_IF_INLINE  0x0002

#define XVSX_INLINE_MAX 256

struct xvsx_inode {
    uint16_t mode;
    uint16_t flags;
    uint32_t uid;
    uint32_t gid;
    uint32_t nlink;
    uint64_t size;
    uint64_t blocks;
    int64_t  atime, mtime, ctime, crtime;
    uint32_t atime_ns, mtime_ns, ctime_ns, crtime_ns;
    uint32_t generation;
    uint32_t rdev;
    uint32_t csum;
    uint8_t  pad0[36];
    uint8_t  data[256];
    uint8_t  xattr[128];
} __attribute__((packed));
_Static_assert(sizeof(struct xvsx_inode) == XVSX_INODE_SIZE, "inode size");

#define XVSX_EXT_MAGIC   0x5845
#define XVSX_EXT_ROOT_MAX 20
#define XVSX_EXT_NODE_MAX 339
#define XVSX_EXT_MAX_DEPTH 4

struct xvsx_ext_header {
    uint16_t magic;
    uint16_t entries;
    uint16_t max;
    uint16_t depth;
    uint8_t  reserved[8];
} __attribute__((packed));
_Static_assert(sizeof(struct xvsx_ext_header) == 16, "ext header size");

struct xvsx_ext {
    uint32_t lblock;
    uint32_t len;
    uint32_t pblock;
} __attribute__((packed));
_Static_assert(sizeof(struct xvsx_ext) == 12, "extent size");
#define XVSX_EXT_NODE_CSUM_OFF 4092

#define XVSX_DIR_FIRST_LEN 4080
#define XVSX_DIR_TAIL_OFF  4080
#define XVSX_DT_UNKNOWN 0
#define XVSX_DT_CHR     2
#define XVSX_DT_DIR     4
#define XVSX_DT_BLK     6
#define XVSX_DT_REG     8
#define XVSX_DT_LNK     10
#define XVSX_DT_TAIL    0xDE

struct xvsx_dirent {
    uint32_t ino;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  type;
    char     name[];
} __attribute__((packed));
#define XVSX_DIRENT_LEN(namelen) ((8 + (namelen) + 7) & ~7)

#define XVSX_JMAGIC "XVSXJRNL"
#define XVSX_JSTATE_CLEAN 1
struct xvsx_jsuper {
    char     magic[8];
    uint32_t version;
    uint32_t nblocks;
    uint32_t first;
    uint32_t head;
    uint64_t seq;
    uint32_t state;
    uint8_t  reserved[4056];
    uint32_t csum;
} __attribute__((packed));
_Static_assert(sizeof(struct xvsx_jsuper) == XVSX_BLOCK_SIZE, "jsuper size");

uint32_t xvsx_crc32c(uint32_t seed, const void *data, uint32_t len);

#endif
