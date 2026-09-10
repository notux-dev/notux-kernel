#include "vmm.h"
#include "pmm.h"
#include "stdint.h"
#define PML4_IDX(addr) (((addr) >> 39) & 0x1FF)
#define PDP_IDX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_IDX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_IDX(addr)   (((addr) >> 12) & 0x1FF)
static uint64_t *kernel_pml4 = 0;
static void zero_table(uint64_t *table)
{
    if (!table)
        return;
    for (int i = 0; i < 512; i++)
        table[i] = 0;
}
static uint64_t *get_next_level(
    uint64_t *entry,
    int allocate
)
{
    if (!entry)
        return 0;
    if (*entry & PAGE_PRESENT)
    {
        if (*entry & PAGE_HUGE)
            return 0;
        return (uint64_t *)(*entry & ~0xFFFULL);
    }
    if (!allocate)
        return 0;
    uint64_t new_table =
        (uint64_t)pmm_alloc_page();
    if (!new_table)
        return 0;
    uint64_t *ptr =
        (uint64_t *)new_table;
    zero_table(ptr);
    *entry =
        new_table |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;
    return ptr;
}
static uint64_t *split_huge_pde(
    uint64_t *pde
)
{
    if (!pde)
        return 0;
    if (!(*pde & PAGE_PRESENT))
        return 0;
    if (!(*pde & PAGE_HUGE))
    {
        return (uint64_t *)
            (*pde & ~0xFFFULL);
    }
    uint64_t huge_phys =
        *pde & ~0x1FFFFFULL;
    uint64_t old_flags =
        *pde &
        (PAGE_PRESENT |
         PAGE_WRITE |
         PAGE_USER);
    uint64_t pt_phys =
        (uint64_t)pmm_alloc_page();
    if (!pt_phys)
        return 0;
    uint64_t *pt =
        (uint64_t *)pt_phys;
    for (uint64_t i = 0;
         i < 512;
         i++)
    {
        uint64_t phys =
            huge_phys +
            i * PAGE_SIZE;
        pt[i] =
            phys |
            old_flags;
    }
    *pde =
        pt_phys |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;
    return pt;
}
void vmm_init(void)
{
    uint64_t cr3;
    asm volatile(
        "mov %%cr3, %0"
        : "=r"(cr3)
    );
    kernel_pml4 =
        (uint64_t *)(cr3 & ~0xFFFULL);
}
uint64_t *vmm_get_kernel_pml4(void)
{
    return kernel_pml4;
}
void vmm_map_page(
    uint64_t *pml4,
    uint64_t virt,
    uint64_t phys,
    uint64_t flags
)
{
    if (!pml4)
        return;
    uint64_t *pdp =
        get_next_level(
            &pml4[PML4_IDX(virt)],
            1
        );
    if (!pdp)
        return;
    uint64_t *pd =
        get_next_level(
            &pdp[PDP_IDX(virt)],
            1
        );
    if (!pd)
        return;
    uint64_t *pde =
        &pd[PD_IDX(virt)];
    uint64_t *pt;
    if (*pde & PAGE_PRESENT)
    {
        if (*pde & PAGE_HUGE)
        {
            pt =
                split_huge_pde(
                    pde
                );
        }
        else
        {
            pt =
                (uint64_t *)
                (*pde & ~0xFFFULL);
        }
    }
    else
    {
        uint64_t new_pt =
            (uint64_t)pmm_alloc_page();
        if (!new_pt)
            return;
        pt =
            (uint64_t *)new_pt;
        zero_table(pt);
        *pde =
            new_pt |
            PAGE_PRESENT |
            PAGE_WRITE |
            PAGE_USER;
    }
    if (!pt)
        return;
    pt[PT_IDX(virt)] =
        (phys & ~0xFFFULL) |
        flags |
        PAGE_PRESENT;
    asm volatile(
        "invlpg (%0)"
        :
        : "r"(virt)
        : "memory"
    );
}
void vmm_unmap_page(
    uint64_t *pml4,
    uint64_t virt
)
{
    if (!pml4)
        return;
    uint64_t *pdp =
        get_next_level(
            &pml4[PML4_IDX(virt)],
            0
        );
    if (!pdp)
        return;
    uint64_t *pd =
        get_next_level(
            &pdp[PDP_IDX(virt)],
            0
        );
    if (!pd)
        return;
    uint64_t *pde =
        &pd[PD_IDX(virt)];
    uint64_t *pt;
    if (*pde & PAGE_HUGE)
    {
        pt =
            split_huge_pde(
                pde
            );
    }
    else
    {
        if (!(*pde & PAGE_PRESENT))
            return;
        pt =
            (uint64_t *)
            (*pde & ~0xFFFULL);
    }
    if (!pt)
        return;
    pt[PT_IDX(virt)] = 0;
    asm volatile(
        "invlpg (%0)"
        :
        : "r"(virt)
        : "memory"
    );
}
uint64_t vmm_virt_to_phys(
    uint64_t *pml4,
    uint64_t virt
)
{
    if (!pml4)
        return 0;
    uint64_t *pdp =
        get_next_level(
            &pml4[PML4_IDX(virt)],
            0
        );
    if (!pdp)
        return 0;
    uint64_t *pd =
        get_next_level(
            &pdp[PDP_IDX(virt)],
            0
        );
    if (!pd)
        return 0;
    uint64_t pde =
        pd[PD_IDX(virt)];
    if (!(pde & PAGE_PRESENT))
        return 0;
    if (pde & PAGE_HUGE)
    {
        uint64_t base =
            pde & ~0x1FFFFFULL;
        return base |
            (virt & 0x1FFFFFULL);
    }
    uint64_t *pt =
        (uint64_t *)
        (pde & ~0xFFFULL);
    if (!pt)
        return 0;
    uint64_t pte =
        pt[PT_IDX(virt)];
    if (!(pte & PAGE_PRESENT))
        return 0;
    return
        (pte & ~0xFFFULL) |
        (virt & 0xFFFULL);
}
uint64_t *create_user_pml4(void)
{
    uint64_t *user_pml4 =
        (uint64_t *)pmm_alloc_page();
    if (!user_pml4)
        return 0;
    zero_table(
        user_pml4
    );
    uint64_t *k_pml4 =
        vmm_get_kernel_pml4();
    if (!k_pml4)
        return 0;
    uint64_t k_pml4e =
        k_pml4[0];
    if (!(k_pml4e & PAGE_PRESENT))
        return user_pml4;
    uint64_t *k_pdp =
        (uint64_t *)
        (k_pml4e & ~0xFFFULL);
    if (!k_pdp)
        return user_pml4;
    uint64_t *user_pdp =
        (uint64_t *)pmm_alloc_page();
    if (!user_pdp)
        return 0;
    zero_table(
        user_pdp
    );
    user_pml4[0] =
        ((uint64_t)user_pdp) |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;
    for (int pdpt_idx = 0;
         pdpt_idx < 4;
         pdpt_idx++)
    {
        if (!(k_pdp[pdpt_idx] & PAGE_PRESENT))
            continue;
        uint64_t *k_pd_n =
            (uint64_t *)
            (k_pdp[pdpt_idx] & ~0xFFFULL);
        if (!k_pd_n)
            continue;
        uint64_t *user_pd_n =
            (uint64_t *)pmm_alloc_page();
        if (!user_pd_n)
            return 0;
        zero_table(
            user_pd_n
        );
        user_pdp[pdpt_idx] =
            ((uint64_t)user_pd_n) |
            PAGE_PRESENT |
            PAGE_WRITE |
            PAGE_USER;
        for (int i = 0;
             i < 512;
             i++)
        {
            user_pd_n[i] =
                k_pd_n[i];
        }
    }
    for (int i = 1;
         i < 512;
         i++)
    {
        user_pml4[i] =
            k_pml4[i];
    }
    return user_pml4;
}
void vmm_switch_pml4(
    uint64_t *pml4
)
{
    if (!pml4)
        return;
    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"((uint64_t)pml4)
        : "memory"
    );
}
