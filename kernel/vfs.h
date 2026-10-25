#ifndef VFS_H
#define VFS_H
#include "types.h"
void vfs_init(const uint8_t *disk_image);
int vfs_open(const char *name);
uint32_t vfs_size(int fd);
const uint8_t *vfs_data(int fd);
#endif
