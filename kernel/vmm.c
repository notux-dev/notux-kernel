#include "vmm.h"
#include "shpage.h"
#include "pmm.h"
#include "stdint.h"
#define PML4_IDX(addr) (((addr) >> 39) & 0x1FF)
#define PDP_IDX(addr) (((addr) >> 30) & 0x1FF)
#define PD_IDX(addr) (((addr) >> 21) & 0x1FF)
#define PT_IDX(addr) (((addr) >> 12) & 0x1FF)
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
static uint64_t kpde(int a, int b, int c);
static int shared_pdpt(int a, int b, uint64_t e)
{
    uint64_t *k = vmm_get_kernel_pml4();
    return a == 0 && b >= 4 && (e & PAGE_PRESENT) && k && (k[0] & PAGE_PRESENT) &&
           e == ((uint64_t *)(k[0] & ~0xFFFULL))[b];
}
static uint64_t *pt_slot(uint64_t *pml4, uint64_t virt)
{
    if (!pml4)
        return 0;
    if (pml4 != kernel_pml4 && PML4_IDX(virt) == 0 && PDP_IDX(virt) >= 4 &&
        shared_pdpt(0, (int)PDP_IDX(virt), ((uint64_t *)(pml4[0] & ~0xFFFULL))[PDP_IDX(virt)]))
        return 0;
    uint64_t *pdp =
        get_next_level(
            &pml4[PML4_IDX(virt)],
            1
        );
    if (!pdp)
        return 0;
    uint64_t *pd =
        get_next_level(
            &pdp[PDP_IDX(virt)],
            1
        );
    if (!pd)
        return 0;
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
            if (pml4 != kernel_pml4 &&
                *pde == kpde((int)PML4_IDX(virt), (int)PDP_IDX(virt),
                             (int)PD_IDX(virt)))
            {
                uint64_t *cp = (uint64_t *)pmm_alloc_page();
                if (!cp)
                    return 0;
                for (int i = 0; i < 512; i++)
                    cp[i] = pt[i];
                *pde = (uint64_t)cp | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
                pt = cp;
            }
        }
    }
    else
    {
        uint64_t new_pt =
            (uint64_t)pmm_alloc_page();
        if (!new_pt)
            return 0;
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
        return 0;
    return &pt[PT_IDX(virt)];
}
void vmm_map_page(
    uint64_t *pml4,
    uint64_t virt,
    uint64_t phys,
    uint64_t flags
)
{
    uint64_t *slot = pt_slot(pml4, virt);
    if (!slot)
        return;
    *slot =
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
int vmm_map_lazy(uint64_t *pml4, uint64_t virt, uint64_t flags)
{
    uint64_t *slot = pt_slot(pml4, virt);
    if (!slot)
        return -1;
    *slot = (flags & (PAGE_USER | PAGE_WRITE | PAGE_OWNED | PAGE_PROTNONE)) | PAGE_LAZY;
    return 0;
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
static uint64_t vmm_virt_to_phys_nolazy(uint64_t *pml4, uint64_t virt);
int vmm_materialize(uint64_t *pml4, uint64_t virt)
{
    uint64_t *p = vmm_pte(pml4, virt);
    if (!p || !(*p & PAGE_OWNED) || !(*p & PAGE_LAZY))
        return 0;
    uint8_t *np = (uint8_t *)pmm_alloc_page();
    if (!np)
        return -1;
    page_zero(np);
    uint64_t e = *p & (PAGE_USER | PAGE_WRITE | PAGE_OWNED | PAGE_PROTNONE);
    *p = (uint64_t)np | e | ((e & PAGE_PROTNONE) ? 0 : PAGE_PRESENT);
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 1;
}
int vmm_lazy_fault(uint64_t *pml4, uint64_t virt, int write)
{
    uint64_t *p = vmm_pte(pml4, virt);
    if (!p)
        return 0;
    uint64_t e = *p;
    if (!(e & PAGE_OWNED) || !(e & PAGE_LAZY) || (e & PAGE_PROTNONE))
        return 0;
    if (write && !(e & PAGE_WRITE))
        return 0;
    return vmm_materialize(pml4, virt);
}
uint64_t vmm_virt_to_phys(
    uint64_t *pml4,
    uint64_t virt
)
{
    uint64_t ph = vmm_virt_to_phys_nolazy(pml4, virt);
    if (ph)
        return ph;
    {
        uint64_t *p = (virt <= 0x7FFFFFFFFFFFULL) ? vmm_pte(pml4, virt) : 0;
        if (p && (*p & PAGE_OWNED) && (*p & PAGE_LAZY) && !(*p & PAGE_PROTNONE) &&
            vmm_materialize(pml4, virt) > 0)
            return vmm_virt_to_phys_nolazy(pml4, virt);
    }
    return 0;
}
static uint64_t vmm_virt_to_phys_nolazy(
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
        (pte & 0x000FFFFFFFFFF000ULL) |
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
    for (int pdpt_idx = 4; pdpt_idx < 512; pdpt_idx++)
        if (k_pdp[pdpt_idx] & PAGE_PRESENT)
            user_pdp[pdpt_idx] = k_pdp[pdpt_idx];
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
#define TBL(e) ((uint64_t *)((e) & 0x000FFFFFFFFFF000ULL))
static uint64_t canon(uint64_t a)
{
    return (a & (1ULL << 47)) ? (a | 0xFFFF000000000000ULL) : a;
}
static uint64_t kpde(int a, int b, int c)
{
    uint64_t *k = vmm_get_kernel_pml4();
    if (!k || !(k[a] & PAGE_PRESENT)) return 0;
    uint64_t *pdpt = TBL(k[a]);
    if (!(pdpt[b] & PAGE_PRESENT)) return 0;
    return TBL(pdpt[b])[c];
}
uint64_t *vmm_pte(uint64_t *pml4, uint64_t virt)
{
    if (!pml4)
        return 0;
    int a = (int)PML4_IDX(virt), b = (int)PDP_IDX(virt), c = (int)PD_IDX(virt);
    uint64_t *pdp = get_next_level(&pml4[a], 0);
    if (!pdp)
        return 0;
    uint64_t *pd = get_next_level(&pdp[b], 0);
    if (!pd)
        return 0;
    uint64_t pde = pd[c];
    if (!(pde & PAGE_PRESENT) || (pde & PAGE_HUGE))
        return 0;
    if (pde == kpde(a, b, c))
        return 0;
    return &TBL(pde)[PT_IDX(virt)];
}
static uint16_t *cowtab;
static uint32_t cow_n;
static int cow_init(void)
{
    if (cowtab)
        return 1;
    uint32_t n = pmm_max_pages();
    uint32_t pages = (n * 2 + PAGE_SIZE - 1) / PAGE_SIZE;
    uint16_t *t = pmm_alloc_contig(pages);
    if (!t)
        return 0;
    for (uint64_t i = 0; i < (uint64_t)pages * PAGE_SIZE / 8; i++)
        ((uint64_t *)t)[i] = 0;
    cow_n = n;
    cowtab = t;
    return 1;
}
static int cow_ref(uint64_t phys)
{
    uint64_t i = phys >> 12;
    if (!cow_init() || i >= cow_n || cowtab[i] == 0xFFFF)
        return 0;
    cowtab[i]++;
    return 1;
}
static int cow_release(uint64_t phys)
{
    uint64_t i = phys >> 12;
    if (cowtab && i < cow_n && cowtab[i] > 0)
    {
        cowtab[i]--;
        return 0;
    }
    return 1;
}
static int cow_shared(uint64_t phys)
{
    uint64_t i = phys >> 12;
    return cowtab && i < cow_n && cowtab[i] > 0;
}
static void frame_release(uint64_t e)
{
    if (e & PAGE_SHARED)
        shp_unref((uint64_t)TBL(e));
    else if (!(e & PAGE_COW) || cow_release((uint64_t)TBL(e)))
        pmm_free_page((void *)TBL(e));
}
int vmm_cow_fault(uint64_t *pml4, uint64_t virt)
{
    uint64_t *p = vmm_pte(pml4, virt);
    if (!p)
        return 0;
    uint64_t e = *p;
    if (!(e & PAGE_OWNED) || !(e & PAGE_COW) || !(e & PAGE_PRESENT) || !(e & PAGE_COWW))
        return 0;
    uint64_t phys = (uint64_t)TBL(e);
    if (!cow_shared(phys))
        *p = (e & ~(PAGE_COW | PAGE_COWW)) | PAGE_WRITE;
    else
    {
        uint8_t *np = (uint8_t *)pmm_alloc_page();
        if (!np)
            return -1;
        page_copy(np, (const void *)phys);
        cow_release(phys);
        *p = ((uint64_t)np) | (e & 0xFFFULL) | PAGE_WRITE;
    }
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 1;
}
uint64_t vmm_virt_to_phys_w(uint64_t *pml4, uint64_t virt)
{
    uint64_t *p = vmm_pte(pml4, virt);
    if (p && (*p & PAGE_OWNED) && (*p & PAGE_COW) && (*p & PAGE_PRESENT))
        if (vmm_cow_fault(pml4, virt) < 0)
            return 0;
    return vmm_virt_to_phys(pml4, virt);
}
int vmm_free_page(uint64_t *pml4, uint64_t virt)
{
    uint64_t *p = vmm_pte(pml4, virt);
    if (!p)
        return 0;
    uint64_t e = *p;
    if (e & PAGE_DEVICE)
    {
        *p = 0;
        asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
        return 1;
    }
    if (!(e & PAGE_OWNED))
        return 0;
    if (PAGE_HASFRAME(e))
        frame_release(e);
    *p = 0;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 1;
}
uint64_t vmm_detach_page(uint64_t *pml4, uint64_t virt)
{
    uint64_t *p = vmm_pte(pml4, virt);
    if (!p)
        return 0;
    uint64_t e = *p;
    if (!(e & PAGE_OWNED) || !PAGE_MAPPED(e))
        return 0;
    *p = 0;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return e;
}
int vmm_protect_page(uint64_t *pml4, uint64_t virt, int write, int none)
{
    uint64_t *p = vmm_pte(pml4, virt);
    if (!p)
        return -1;
    uint64_t e = *p;
    if (!(e & PAGE_OWNED) || !PAGE_MAPPED(e))
        return -1;
    if (e & PAGE_LAZY)
    {
        e &= ~(uint64_t)(PAGE_WRITE | PAGE_PROTNONE);
        if (none)
            e |= PAGE_PROTNONE;
        else if (write)
            e |= PAGE_WRITE;
        *p = e;
        return 0;
    }
    e &= ~(uint64_t)(PAGE_WRITE | PAGE_PRESENT | PAGE_PROTNONE | PAGE_COWW);
    if (none)
        e |= PAGE_PROTNONE;
    else
    {
        e |= PAGE_PRESENT;
        if (write)
            e |= (e & PAGE_COW) ? PAGE_COWW : PAGE_WRITE;
    }
    *p = e;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}
int vmm_copy_user_space(uint64_t *src, uint64_t *dst)
{
    uint64_t *k = vmm_get_kernel_pml4();
    for (int a = 0; a < 512; a++)
    {
        if (!(src[a] & PAGE_PRESENT) || (k && src[a] == k[a]))
            continue;
        uint64_t *pdpt = TBL(src[a]);
        for (int b = 0; b < 512; b++)
        {
            if (!(pdpt[b] & PAGE_PRESENT) || shared_pdpt(a, b, pdpt[b]))
                continue;
            uint64_t *pd = TBL(pdpt[b]);
            for (int c = 0; c < 512; c++)
            {
                if (!(pd[c] & PAGE_PRESENT) || (pd[c] & PAGE_HUGE) ||
                    pd[c] == kpde(a, b, c))
                    continue;
                uint64_t *pt = TBL(pd[c]);
                for (int d = 0; d < 512; d++)
                {
                    uint64_t e = pt[d];
                    if ((e & PAGE_DEVICE) && PAGE_MAPPED(e))
                    {
                        uint64_t dv = canon(((uint64_t)a << 39) | ((uint64_t)b << 30) | ((uint64_t)c << 21) |
                                            ((uint64_t)d << 12));
                        vmm_map_page(dst, dv, e & 0x000FFFFFFFFFF000ULL, e & (PAGE_USER | PAGE_WRITE | PAGE_DEVICE));
                        continue;
                    }
                    if (!(e & PAGE_OWNED) || !PAGE_MAPPED(e))
                        continue;
                    uint64_t virt = canon(((uint64_t)a << 39) |
                        ((uint64_t)b << 30) | ((uint64_t)c << 21) |
                        ((uint64_t)d << 12));
                    if (e & PAGE_LAZY)
                    {
                        if (vmm_map_lazy(dst, virt, e) < 0)
                            return -1;
                        continue;
                    }
                    uint8_t *np;
                    if (e & PAGE_SHARED)
                    {
                        np = (uint8_t *)(e & 0x000FFFFFFFFFF000ULL);
                        shp_ref((uint64_t)np);
                        vmm_map_page(dst, virt, (uint64_t)np,
                                     (e & 0xFFFULL & ~PAGE_PRESENT));
                        if (e & PAGE_PROTNONE)
                        {
                            uint64_t *sp = vmm_pte(dst, virt);
                            if (sp)
                                *sp &= ~(uint64_t)PAGE_PRESENT;
                        }
                        continue;
                    }
                    {
                        uint64_t phys = e & 0x000FFFFFFFFFF000ULL;
                        int wr = (e & PAGE_WRITE) || (e & PAGE_COWW);
                        if (cow_ref(phys))
                        {
                            uint64_t ne = (e & ~(uint64_t)PAGE_WRITE) | PAGE_COW;
                            if (wr)
                                ne |= PAGE_COWW;
                            pt[d] = ne;
                            vmm_map_page(dst, virt, phys, ne & PAGE_FLAGMASK & ~(uint64_t)PAGE_PRESENT);
                        }
                        else
                        {
                            np = (uint8_t *)pmm_alloc_page();
                            if (!np)
                                return -1;
                            page_copy(np, (const void *)phys);
                            vmm_map_page(dst, virt, (uint64_t)np,
                                         (e & 0xFFFULL & ~PAGE_PRESENT) | (wr ? PAGE_WRITE : 0));
                        }
                    }
                    if (e & PAGE_PROTNONE)
                    {
                        uint64_t *np_pte = vmm_pte(dst, virt);
                        if (np_pte)
                            *np_pte &= ~(uint64_t)PAGE_PRESENT;
                    }
                }
            }
        }
    }
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" : : : "rax", "memory");
    return 0;
}
void vmm_free_user_space(uint64_t *pml4)
{
    if (!pml4)
        return;
    uint64_t *k = vmm_get_kernel_pml4();
    for (int a = 0; a < 512; a++)
    {
        if (!(pml4[a] & PAGE_PRESENT) || (k && pml4[a] == k[a]))
            continue;
        uint64_t *pdpt = TBL(pml4[a]);
        for (int b = 0; b < 512; b++)
        {
            if (!(pdpt[b] & PAGE_PRESENT) || shared_pdpt(a, b, pdpt[b]))
                continue;
            uint64_t *pd = TBL(pdpt[b]);
            for (int c = 0; c < 512; c++)
            {
                if (!(pd[c] & PAGE_PRESENT) || (pd[c] & PAGE_HUGE) ||
                    pd[c] == kpde(a, b, c))
                    continue;
                uint64_t *pt = TBL(pd[c]);
                for (int d = 0; d < 512; d++)
                {
                    if ((pt[d] & PAGE_OWNED) && PAGE_HASFRAME(pt[d]))
                    {
                        frame_release(pt[d]);
                    }
                }
                pmm_free_page(pt);
            }
            pmm_free_page(pd);
        }
        pmm_free_page(pdpt);
    }
    pmm_free_page(pml4);
}
