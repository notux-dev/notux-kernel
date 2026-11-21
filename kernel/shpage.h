#ifndef SHPAGE_H
#define SHPAGE_H 
#include "stdint.h"
struct inode;
void *shp_anon(void);
void *shp_file(struct inode *ip, uint64_t off);
void shp_ref(uint64_t phys);
void shp_unref(uint64_t phys);
void shp_sync(uint64_t phys);
#endif
