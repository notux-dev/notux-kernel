#include "vfs.h"
#include "ramfs.h"
void vfs_init(const uint8_t *disk_image) {
    ramfs_init(disk_image);
}
int vfs_open(const char *name) {
    return ramfs_open(name);
}
uint32_t vfs_size(int fd) {
    return ramfs_size(fd);
}
const uint8_t *vfs_data(int fd) {
    return ramfs_data(fd);
}
