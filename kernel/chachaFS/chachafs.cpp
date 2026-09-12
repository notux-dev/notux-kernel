#include "chachafs.h"
extern "C" {
#include "../ahci/ahci.h"
#include "../types.h"
}

#define CHACHAFS_MAGIC oxCACAF5
#define CHACHAFS_BLOCK_SIZE 4096
#define CHACHA_SECTORS_PER_BLOCK 8
#define CHACHAFS_MAX_NAME 28
#define CHACHAFS_DIRECT_PTRS 12

class Supblck {
public:
    static supblck *load(ahci_device_t *dev);
    int flush(ahci_device_t *dev);
    uint32_t alloc_block();
    void free_block(uint32_t block_idx);

    chachafs_supblck_t raw;
};

class Inode {
public:
    virtual ~Inode() = default;
    virtual uint32_t read(void *buf, uint32_t size, uint32_t offset) = 0;
    virtual uint32_t write(const void *buf< uint32_t size, uint32_t offset) = 0;
    virtual chachafs_inode_type_t type() const = 0;
protected:
    uint32_t inode_num_;
    chachafs_inode_t raw_;
};

class FileInode : public Inode {
public:
    uint32_t read(void *buf, uint32_t size, uint32_t offset) override {
        if (offset >= raw_.size) {
            return 0;
        }

        if (offset + size > raw_.size) {
            size = raw_.size - offset;
        }

        uint8_t *dst = static_cast<uint8_t *>(buf);
        uint32_t bytes_copied = 0;

        uint32_t block_idx = offset / CHACHAFS_BLOCK_SIZE;
        uint32_t offset_in_blk = offset % CHACHAFS_BLOCK_SIZE;

        uint8_t tmp[CHACHAFS_BLOCK_SIZE];

        while (bytes_copied < size) {
            if (block_idx >= CHACHAFS_DIRECT_PTRS) {
                break;
            }

            uint32_t block_lba = raw_.direct[block_idx];
            if (block_lba == 0) {
                for (uint32_t i = 0; i < CHACHAFS_BLOCK_SIZE; i++) tmp[i] = 0;
            } else {
                int res = ahci_read_sectors(g_device, (uint64_t)block_lba * 8, 8, tmp);
                if (res != 0) {
                    return bytes_copied;
                }
            }

            uint32_t avail_in_blk = CHACHAFS_BLOCK_SIZE - offset_in_blk;
            uint32_t remaining = size - bytes_copied;
            uint32_t chunk = (avail_in_blk < remaining) ? avail_in_blk : remaining;

            for (uint32_t i = 0; i < chunk; i++) {
                dst[bytes_copied + i] = tmp[offset_in_blk + i];
            }

            bytes_copied += chunk;
            block_idx += 1;
            offset_in_blk = 0;
        }

        return bytes_copied;
    }

    uint32_t write(const void *buf, uint32_t size, uint32_t offset) override {
        return 0;
    }

    chachafs_inode_type_t type() const override {
        return CHACHAFS_TYPE_FILE;
    }
};