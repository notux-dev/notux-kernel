#ifndef PMM_H
#define PMM_H
#include "stdint.h"
void pmm_init(uint32_t reserved_pages);
void pmm_reserve_region(uint64_t phys_start, uint64_t phys_end);
void* pmm_alloc_page(void);
void pmm_free_page(void* ptr);
void* pmm_alloc_contig(uint32_t n);
void pmm_free_contig(void* ptr, uint32_t n);
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);
void pmm_set_limit(uint64_t bytes);
#endif
