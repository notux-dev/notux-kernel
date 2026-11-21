#include "ramfs.h"
static const uint8_t *disk = 0;
static ramfs_header_t header;
static ramfs_entry_t *entries = 0;
static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}
void ramfs_init(const uint8_t *disk_image) {
    disk = disk_image;
    header.magic = *(uint32_t*)(disk + 0);
    header.file_count = *(uint32_t*)(disk + 4);
    entries = (ramfs_entry_t*)(disk + 8);
}
int ramfs_open(const char *name) {
    if (!disk || header.magic != RAMFS_MAGIC) return -1;
    for (uint32_t i = 0; i < header.file_count; i++) {
        if (streq(entries[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}
uint32_t ramfs_size(int fd) {
    if (fd < 0) return 0;
    return entries[fd].size;
}
const uint8_t *ramfs_data(int fd) {
    if (fd < 0) return 0;
    return disk + entries[fd].offset;
}
