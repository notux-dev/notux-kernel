#include "stdint.h"
#define PAGE_SIZE 4096
#define MAX_PAGES (128 * 1024 * 1024 / PAGE_SIZE)
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
void *pmm_alloc_page(void)
{
    for (uint32_t i = 0;
         i < MAX_PAGES;
         i++)
    {
        if (!bitmap_test(i))
        {
            bitmap_set(i);
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
}
