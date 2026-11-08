#include "uaccess.h"
#include "vmm.h"

#define USER_MAX 0x7FFFFFFFFFFFULL

int copy_from_user(process_t *p, void *dst, uint64_t va, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n)
    {
        if (va > USER_MAX)
            return -1;
        uint64_t ph = vmm_virt_to_phys(p->pml4, va);
        if (!ph)
            return -1;
        uint64_t chunk = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (chunk > n)
            chunk = n;
        const uint8_t *s = (const uint8_t *)ph;
        for (uint64_t i = 0; i < chunk; i++)
            d[i] = s[i];
        d += chunk; va += chunk; n -= chunk;
    }
    return 0;
}

int copy_to_user(process_t *p, uint64_t va, const void *src, uint64_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    while (n)
    {
        if (va > USER_MAX)
            return -1;
        uint64_t ph = vmm_virt_to_phys(p->pml4, va);
        if (!ph)
            return -1;
        uint64_t chunk = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (chunk > n)
            chunk = n;
        uint8_t *d = (uint8_t *)ph;
        for (uint64_t i = 0; i < chunk; i++)
            d[i] = s[i];
        s += chunk; va += chunk; n -= chunk;
    }
    return 0;
}

int strncpy_from_user(process_t *p, char *dst, uint64_t va, int max)
{
    for (int i = 0; i < max; i++)
    {
        if (copy_from_user(p, &dst[i], va + i, 1) < 0)
            return -1;
        if (!dst[i])
            return i;
    }
    return -1;
}
