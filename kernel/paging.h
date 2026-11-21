#ifndef PAGING_H
#define PAGING_H 
#include <stdint.h>
void paging_init(void);
void paging_enable(void);
void paging_map_ram(uint64_t top);
#endif
