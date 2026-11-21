#ifndef VMM_H
#define VMM_H 
#include "stdint.h"
#define PAGE_PRESENT (1 << 0)
#define PAGE_WRITE (1 << 1)
#define PAGE_USER (1 << 2)
#define PAGE_HUGE (1 << 7)
#define PAGE_OWNED (1 << 9)
#define PAGE_PROTNONE (1 << 10)
#define PAGE_SHARED (1 << 11)
#define PAGE_COW (1ULL << 52)
#define PAGE_COWW (1ULL << 53)
#define PAGE_LAZY (1ULL << 54)
#define PAGE_DEVICE (1ULL << 55)
#define PAGE_FLAGMASK (0xFFFULL | PAGE_COW | PAGE_COWW)
#define PAGE_HASFRAME(e) (((e) & (PAGE_PRESENT | PAGE_PROTNONE)) && !((e) & PAGE_LAZY))
#define PAGE_MAPPED(e) ((e) & (PAGE_PRESENT | PAGE_PROTNONE | PAGE_LAZY))
#define PAGE_SIZE 4096
static inline void page_zero(void *p)
{
    uint64_t n = PAGE_SIZE / 8;
    asm volatile("rep stosq" : "+D"(p), "+c"(n) : "a"(0ULL) : "memory");
}
static inline void page_copy(void *dst, const void *src)
{
    uint64_t n = PAGE_SIZE / 8;
    asm volatile("rep movsq" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
}
void vmm_init(void);
uint64_t* vmm_get_kernel_pml4(void);
void vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t *pml4, uint64_t virt);
uint64_t vmm_virt_to_phys(uint64_t *pml4, uint64_t virt);
uint64_t* create_user_pml4(void);
void vmm_switch_pml4(uint64_t *pml4);
int vmm_copy_user_space(uint64_t *src, uint64_t *dst);
void vmm_free_user_space(uint64_t *pml4);
uint64_t *vmm_pte(uint64_t *pml4, uint64_t virt);
int vmm_free_page(uint64_t *pml4, uint64_t virt);
int vmm_protect_page(uint64_t *pml4, uint64_t virt, int write, int none);
uint64_t vmm_detach_page(uint64_t *pml4, uint64_t virt);
uint64_t vmm_virt_to_phys_w(uint64_t *pml4, uint64_t virt);
int vmm_cow_fault(uint64_t *pml4, uint64_t virt);
int vmm_map_lazy(uint64_t *pml4, uint64_t virt, uint64_t flags);
int vmm_materialize(uint64_t *pml4, uint64_t virt);
int vmm_lazy_fault(uint64_t *pml4, uint64_t virt, int write);
#endif
