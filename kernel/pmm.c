#include "stdint.h"
#define PAGE_SIZE 4096
#define MAX_PAGES (1024ULL * 1024 * 1024 / PAGE_SIZE)
static uint32_t alloc_hint;
static uint32_t total_pages = MAX_PAGES;
static uint8_t bitmap[MAX_PAGES / 8];
static void bitmap_set(uint32_t page_index)
{
    if (page_index >= MAX_PAGES)
        return;
    bitmap[page_index / 8] |=
        (uint8_t)(1u << (page_index % 8));
}
static void bitmap_clear(uint32_t page_index)
{
    if (page_index >= MAX_PAGES)
        return;
    bitmap[page_index / 8] &=
        (uint8_t)~(1u << (page_index % 8));
}
static int bitmap_test(uint32_t page_index)
{
    if (page_index >= MAX_PAGES)
        return 1;
    return
        bitmap[page_index / 8] &
        (uint8_t)(1u << (page_index % 8));
}
void pmm_init(uint32_t reserved_pages)
{
    for (uint32_t i = 0; i < sizeof(bitmap); i++) {
        bitmap[i] = 0;
    }
    if (reserved_pages > MAX_PAGES)
        reserved_pages = MAX_PAGES;
    for (uint32_t i = 0;
         i < reserved_pages;
         i++)
    {
        bitmap_set(i);
    }
}
void pmm_reserve_region(
    uint64_t phys_start,
    uint64_t phys_end
)
{
    if (phys_end <= phys_start)
        return;
    uint64_t start_page =
        phys_start / PAGE_SIZE;
    uint64_t end_page =
        (phys_end + PAGE_SIZE - 1) /
        PAGE_SIZE;
    if (start_page >= MAX_PAGES)
        return;
    if (end_page > MAX_PAGES)
        end_page = MAX_PAGES;
    for (uint64_t i = start_page;
         i < end_page;
         i++)
    {
        bitmap_set((uint32_t)i);
    }
}
void pmm_set_limit(uint64_t bytes)
{
    uint64_t first = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (first < MAX_PAGES)
        total_pages = (uint32_t)first;
    for (uint64_t i = first; i < MAX_PAGES; i++)
        bitmap_set((uint32_t)i);
}
void *pmm_alloc_page(void)
{
    for (uint32_t i = alloc_hint;
         i < MAX_PAGES;
         i++)
    {
        if (!bitmap_test(i))
        {
            bitmap_set(i);
            alloc_hint = i + 1;
            return (void *)(uint64_t)
                ((uint64_t)i * PAGE_SIZE);
        }
    }
    return 0;
}
void pmm_free_page(void *ptr)
{
    if (!ptr)
        return;
    uint64_t addr =
        (uint64_t)ptr;
    if (addr & (PAGE_SIZE - 1))
        return;
    uint64_t page_index =
        addr / PAGE_SIZE;
    if (page_index >= MAX_PAGES)
        return;
    bitmap_clear(
        (uint32_t)page_index
    );
    if (page_index < alloc_hint)
        alloc_hint = (uint32_t)page_index;
}

void *pmm_alloc_contig(uint32_t n)
{
    if (n == 0)
        return 0;
    uint32_t run = 0;
    for (uint32_t i = 0; i < MAX_PAGES; i++)
    {
        if (bitmap_test(i))
        {
            run = 0;
            continue;
        }
        if (++run == n)
        {
            uint32_t first = i + 1 - n;
            for (uint32_t j = first; j <= i; j++)
                bitmap_set(j);
            return (void *)((uint64_t)first * PAGE_SIZE);
        }
    }
    return 0;
}
void pmm_free_contig(void *ptr, uint32_t n)
{
    for (uint32_t j = 0; j < n; j++)
        pmm_free_page((uint8_t *)ptr + (uint64_t)j * PAGE_SIZE);
}

uint32_t pmm_total_pages(void) { return total_pages; }
uint32_t pmm_free_pages(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < MAX_PAGES; i++)
        if (!bitmap_test(i))
            n++;
    return n;
}
