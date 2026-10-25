#ifndef PMM_H
#define PMM_H
#include "stdint.h"
void pmm_init(uint32_t reserved_pages);
void pmm_reserve_region(uint64_t phys_start, uint64_t phys_end);
void* pmm_alloc_page(void);
void pmm_free_page(void* ptr);
#endif
