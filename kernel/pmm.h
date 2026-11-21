#ifndef PMM_H
#define PMM_H 
#include "stdint.h"
#define PHYS_MAX (64ULL << 30)
typedef struct { uint64_t start, end; } pmm_range_t;
uint64_t pmm_bitmap_bytes(uint64_t top);
uint64_t pmm_init_mem(uint64_t bitmap_phys, const pmm_range_t *avail, int n);
void pmm_reserve_region(uint64_t phys_start, uint64_t phys_end);
void* pmm_alloc_page(void);
void pmm_free_page(void* ptr);
void* pmm_alloc_contig(uint32_t n);
void pmm_free_contig(void* ptr, uint32_t n);
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);
uint32_t pmm_max_pages(void);
extern uint64_t pmm_low_user_end;
#endif
