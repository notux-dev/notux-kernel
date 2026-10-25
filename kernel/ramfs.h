#ifndef RAMFS_H
#define RAMFS_H
#include <stdint.h>
#define RAMFS_MAGIC 0x52414D46 
typedef struct {
    uint32_t magic;
    uint32_t file_count;
} __attribute__((packed)) ramfs_header_t;
typedef struct {
    char name[32];
    uint32_t offset;
    uint32_t size;
} __attribute__((packed)) ramfs_entry_t;
void ramfs_init(const uint8_t *disk_image);
int ramfs_open(const char *name);
uint32_t ramfs_size(int fd);
const uint8_t *ramfs_data(int fd);
#endif
