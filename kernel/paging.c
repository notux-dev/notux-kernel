#include "stdint.h"
#include "pmm.h"
#include "vbe.h"
#include "console.h"
#define PAGE_PRESENT (1 << 0)
#define PAGE_WRITE   (1 << 1)
#define PAGE_USER    (1 << 2)
#define PAGE_HUGE    (1 << 7)
__attribute__((aligned(4096)))
static uint64_t pml4_table[512];
__attribute__((aligned(4096)))
static uint64_t pdp_table[512];
__attribute__((aligned(4096)))
static uint64_t pd_tables[4][512];
void paging_init(void)
{
    for (int i = 0; i < 512; i++)
    {
        pml4_table[i] = 0;
        pdp_table[i] = 0;
    }
    pml4_table[0] =
        ((uint64_t)pdp_table) |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;
    for (int i = 0; i < 4; i++)
    {
        pdp_table[i] =
            ((uint64_t)pd_tables[i]) |
            PAGE_PRESENT |
            PAGE_WRITE |
            PAGE_USER;
        for (int j = 0; j < 512; j++)
        {
            uint64_t phys_addr =
                (i * 0x40000000ull) +
                (j * 0x200000ull);
            pd_tables[i][j] =
                phys_addr |
                PAGE_PRESENT |
                PAGE_WRITE |
                PAGE_USER |
                PAGE_HUGE;
        }
    }
    asm volatile(
        "mov %0, %%rax\n\t"
        "mov %%rax, %%cr3\n\t"
        :
        : "r"((uint64_t)pml4_table)
        : "rax", "memory"
    );
    kprint(
        "initialized: 4GB FU\n",
        0x00000000
    );
}
void paging_enable(void)
{
    uint64_t cr0;
    asm volatile(
        "mov %%cr0, %0"
        : "=r"(cr0)
    );
    cr0 |= (1 << 31);
    asm volatile(
        "mov %0, %%cr0"
        :
        : "r"(cr0)
    );
}
