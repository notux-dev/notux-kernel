#ifndef VMM_H
#define VMM_H
#include "stdint.h"
#define PAGE_PRESENT (1 << 0)
#define PAGE_WRITE   (1 << 1)
#define PAGE_USER    (1 << 2)
#define PAGE_HUGE    (1 << 7)
#define PAGE_OWNED   (1 << 9)
#define PAGE_PROTNONE (1 << 10)
#define PAGE_SIZE 4096
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
#endif
