#include "stdint.h"
#include "pmm.h"
#define PAGE_SIZE 4096
uint64_t pmm_low_user_end = 0x1000000;
static uint8_t *bitmap;
static uint32_t max_pages;
static uint32_t alloc_hint;
static uint32_t total_pages;
static void bitmap_set(uint32_t page_index)
{
    if (page_index >= max_pages)
        return;
    bitmap[page_index / 8] |= (uint8_t)(1u << (page_index % 8));
}
static void bitmap_clear(uint32_t page_index)
{
    if (page_index >= max_pages)
        return;
    bitmap[page_index / 8] &= (uint8_t)~(1u << (page_index % 8));
}
static int bitmap_test(uint32_t page_index)
{
    if (page_index >= max_pages)
        return 1;
    return bitmap[page_index / 8] & (uint8_t)(1u << (page_index % 8));
}
uint64_t pmm_bitmap_bytes(uint64_t top)
{
    if (top > PHYS_MAX)
        top = PHYS_MAX;
    return (((top + PAGE_SIZE - 1) / PAGE_SIZE) + 7) / 8;
}
uint64_t pmm_init_mem(uint64_t bitmap_phys, const pmm_range_t *avail, int n)
{
    uint64_t top = 0;
    for (int i = 0; i < n; i++)
        if (avail[i].end > top)
            top = avail[i].end;
    if (top > PHYS_MAX)
        top = PHYS_MAX;
    bitmap = (uint8_t *)bitmap_phys;
    max_pages = (uint32_t)((top + PAGE_SIZE - 1) / PAGE_SIZE);
    uint64_t bytes = pmm_bitmap_bytes(top);
    for (uint64_t i = 0; i < bytes; i++)
        bitmap[i] = 0xFF;
    total_pages = 0;
    for (int i = 0; i < n; i++)
    {
        uint64_t a = (avail[i].start + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t b = avail[i].end / PAGE_SIZE;
        if (b > max_pages)
            b = max_pages;
        for (uint64_t pg = a; pg < b; pg++)
        {
            bitmap_clear((uint32_t)pg);
            total_pages++;
        }
    }
    pmm_reserve_region(bitmap_phys, bitmap_phys + bytes);
    return top;
}
void pmm_reserve_region(
    uint64_t phys_start,
    uint64_t phys_end
)
{
    if (phys_end <= phys_start)
        return;
    uint64_t start_page = phys_start / PAGE_SIZE;
    uint64_t end_page = (phys_end + PAGE_SIZE - 1) / PAGE_SIZE;
    if (start_page >= max_pages)
        return;
    if (end_page > max_pages)
        end_page = max_pages;
    for (uint64_t i = start_page; i < end_page; i++)
        bitmap_set((uint32_t)i);
}
void *pmm_alloc_page(void)
{
    for (uint32_t i = alloc_hint;
         i < max_pages;
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
    if (page_index >= max_pages)
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
    for (uint32_t i = 0; i < max_pages; i++)
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
    for (uint32_t i = 0; i < max_pages; i++)
        if (!bitmap_test(i))
            n++;
    return n;
}
uint32_t pmm_max_pages(void) { return max_pages; }
