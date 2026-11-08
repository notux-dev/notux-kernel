#include "syscalls.h"
#include "kfile.h"
#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "types.h"
#include "uaccess.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "Xjail/xv6/xv6fs_api.h"

#define MMAP_LO   0x100000000000ULL
#define MMAP_HI   0x600000000000ULL
#define FIXED_LO  0x100000000ULL
#define FIXED_HI  0x700000000000ULL

static uint64_t up(uint64_t x) { return (x + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1); }
#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)

static int vma_cut(process_t *p, uint64_t s, uint64_t e)
{
    for (int i = 0; i < p->nvmas; i++)
    {
        vma_t v = p->vmas[i];
        if (v.end <= s || v.start >= e)
            continue;
        int keep_lo = v.start < s, keep_hi = v.end > e;
        if (keep_lo && keep_hi)
        {
            if (p->nvmas >= MAX_VMAS)
                return -1;
            p->vmas[i].end = s;
            p->vmas[p->nvmas].start = e;
            p->vmas[p->nvmas].end = v.end;
            p->nvmas++;
        }
        else if (keep_lo)
            p->vmas[i].end = s;
        else if (keep_hi)
            p->vmas[i].start = e;
        else
        {
            p->vmas[i] = p->vmas[--p->nvmas];
            i--;
        }
    }
    return 0;
}

static int vma_overlaps(process_t *p, uint64_t s, uint64_t e)
{
    for (int i = 0; i < p->nvmas; i++)
        if (p->vmas[i].start < e && p->vmas[i].end > s)
            return 1;
    return 0;
}

static int vma_add(process_t *p, uint64_t s, uint64_t e)
{
    for (int i = 0; i < p->nvmas; i++)
    {
        if (p->vmas[i].end == s) { p->vmas[i].end = e; goto merged; }
        if (p->vmas[i].start == e) { p->vmas[i].start = s; goto merged; }
    }
    if (p->nvmas >= MAX_VMAS)
        return -1;
    p->vmas[p->nvmas].start = s;
    p->vmas[p->nvmas].end = e;
    p->nvmas++;
    return 0;
merged:
    for (int i = 0; i < p->nvmas; i++)
        for (int j = 0; j < p->nvmas; j++)
            if (i != j && p->vmas[i].end == p->vmas[j].start)
            {
                p->vmas[i].end = p->vmas[j].end;
                p->vmas[j] = p->vmas[--p->nvmas];
                return 0;
            }
    return 0;
}

static uint64_t find_hole(process_t *p, uint64_t len)
{
    uint64_t cand = MMAP_LO;
    for (;;)
    {
        if (cand + len > MMAP_HI)
            return 0;
        int bumped = 0;
        for (int i = 0; i < p->nvmas; i++)
            if (p->vmas[i].start < cand + len && p->vmas[i].end > cand)
            {
                cand = p->vmas[i].end;
                bumped = 1;
            }
        if (!bumped)
            return cand;
    }
}

static void unmap_range(process_t *p, uint64_t s, uint64_t e)
{
    for (uint64_t a = s; a < e; a += PAGE_SIZE)
        vmm_free_page(p->pml4, a);
}

uint64_t sys_mmap(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t addr = regs[9], len = regs[10];
    uint64_t prot = regs[11], flags = regs[5];
    int64_t fd = (int64_t)regs[7];
    uint64_t off = regs[6];

    if (len == 0 || (off & (PAGE_SIZE - 1)))
        RET(-EINVAL);
    if (!(flags & (MAP_SHARED | MAP_PRIVATE)))
        RET(-EINVAL);
    len = up(len);

    struct inode *ip = 0;
    if (!(flags & MAP_ANONYMOUS))
    {
        ip = fd_inode_of(p, fd);
        if (!ip)
            RET(-EBADF);
        if (ip->type != T_FILE)
            RET(-ENODEV);
    }
    int noreplace = (flags & MAP_FIXED_NOREPLACE) != 0;
    if (noreplace)
        flags |= MAP_FIXED;

    uint64_t base;
    if (flags & MAP_FIXED)
    {
        if ((addr & (PAGE_SIZE - 1)) || addr < FIXED_LO ||
            addr + len > FIXED_HI || addr + len < addr)
            RET(-EINVAL);
        base = addr;
        if (noreplace && vma_overlaps(p, base, base + len))
            RET(-EEXIST);
        unmap_range(p, base, base + len);
        if (vma_cut(p, base, base + len) < 0)
            RET(-ENOMEM);
    }
    else
    {
        base = find_hole(p, len);
        if (!base)
            RET(-ENOMEM);
    }
    if (p->nvmas >= MAX_VMAS - 1)
        RET(-ENOMEM);

    uint64_t pf = PAGE_USER | PAGE_OWNED;
    if (prot & PROT_WRITE)
        pf |= PAGE_WRITE;
    for (uint64_t o = 0; o < len; o += PAGE_SIZE)
    {
        uint8_t *pg = (uint8_t *)pmm_alloc_page();
        if (!pg)
        {
            unmap_range(p, base, base + o);
            RET(-ENOMEM);
        }
        for (int i = 0; i < PAGE_SIZE; i++)
            pg[i] = 0;
        if (ip)
        {
            ilock(ip);
            readi(ip, (char *)pg, (uint)(off + o), PAGE_SIZE);
            iunlock(ip);
        }
        vmm_map_page(p->pml4, base + o, (uint64_t)pg, pf);
        if (!(prot & (PROT_READ | PROT_WRITE | PROT_EXEC)))
            vmm_protect_page(p->pml4, base + o, 0, 1);
    }
    vma_add(p, base, base + len);
    RET(base);
}

uint64_t sys_munmap(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t addr = regs[9], len = regs[10];
    if ((addr & (PAGE_SIZE - 1)) || len == 0 || addr + len < addr)
        RET(-EINVAL);
    len = up(len);
    unmap_range(p, addr, addr + len);
    vma_cut(p, addr, addr + len);
    RET(0);
}

uint64_t sys_mprotect(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t addr = regs[9], len = regs[10], prot = regs[11];
    if ((addr & (PAGE_SIZE - 1)) || addr + len < addr)
        RET(-EINVAL);
    if (len == 0)
        RET(0);
    len = up(len);
    for (uint64_t a = addr; a < addr + len; a += PAGE_SIZE)
    {
        uint64_t *pte = vmm_pte(p->pml4, a);
        if (!pte || !(*pte & PAGE_OWNED) ||
            !(*pte & (PAGE_PRESENT | PAGE_PROTNONE)))
            RET(-ENOMEM);
    }
    int none = !(prot & (PROT_READ | PROT_WRITE | PROT_EXEC));
    for (uint64_t a = addr; a < addr + len; a += PAGE_SIZE)
        vmm_protect_page(p->pml4, a, (prot & PROT_WRITE) != 0, none);
    RET(0);
}

uint64_t sys_madvise(uint64_t *regs)
{
    RET(0);
}

uint64_t sys_mem_nop(uint64_t *regs)
{
    RET(0);
}

uint64_t sys_mincore(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t addr = regs[9], len = regs[10], vec = regs[11];
    if (addr & (PAGE_SIZE - 1))
        RET(-EINVAL);
    uint64_t n = up(len) / PAGE_SIZE;
    for (uint64_t i = 0; i < n; i++)
    {
        uint64_t *pte = vmm_pte(p->pml4, addr + i * PAGE_SIZE);
        if (!pte || !(*pte & PAGE_OWNED) ||
            !(*pte & (PAGE_PRESENT | PAGE_PROTNONE)))
            RET(-ENOMEM);
        uint8_t one = 1;
        if (copy_to_user(p, vec + i, &one, 1) < 0)
            RET(-EFAULT);
    }
    RET(0);
}

uint64_t sys_mremap(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t oa = regs[9], ol = regs[10], nl = regs[11], fl = regs[5];
    if ((oa & (PAGE_SIZE - 1)) || nl == 0 || (fl & ~(uint64_t)(MREMAP_MAYMOVE | MREMAP_FIXED)))
        RET(-EINVAL);
    if (fl & MREMAP_FIXED)
        RET(-EINVAL);
    if (ol == 0)
        RET(-EINVAL);
    ol = up(ol);
    nl = up(nl);
    if (oa + ol < oa)
        RET(-EINVAL);
    uint64_t lowest_flags = 0;
    for (uint64_t a = oa; a < oa + ol; a += PAGE_SIZE)
    {
        uint64_t *pte = vmm_pte(p->pml4, a);
        if (!pte || !(*pte & PAGE_OWNED) ||
            !(*pte & (PAGE_PRESENT | PAGE_PROTNONE)))
            RET(-EFAULT);
        lowest_flags = *pte & 0xFFF;
    }
    if (nl == ol)
        RET(oa);
    if (nl < ol)
    {
        unmap_range(p, oa + nl, oa + ol);
        vma_cut(p, oa + nl, oa + ol);
        RET(oa);
    }

    uint64_t tail_flags = lowest_flags & ~(uint64_t)PAGE_PRESENT;
    int none = (lowest_flags & PAGE_PROTNONE) != 0;
    uint64_t base = oa;
    int moved = 0;
    if (oa + nl > FIXED_HI || vma_overlaps(p, oa + ol, oa + nl))
    {
        if (!(fl & MREMAP_MAYMOVE))
            RET(-ENOMEM);
        base = find_hole(p, nl);
        if (!base)
            RET(-ENOMEM);
        moved = 1;
    }
    if (p->nvmas >= MAX_VMAS - 2)
        RET(-ENOMEM);

    uint64_t tail_start = moved ? base + ol : oa + ol;
    uint64_t tail_len = nl - ol;
    for (uint64_t o = 0; o < tail_len; o += PAGE_SIZE)
    {
        uint8_t *pg = (uint8_t *)pmm_alloc_page();
        if (!pg)
        {
            unmap_range(p, tail_start, tail_start + o);
            RET(-ENOMEM);
        }
        for (int i = 0; i < PAGE_SIZE; i++)
            pg[i] = 0;
        vmm_map_page(p->pml4, tail_start + o, (uint64_t)pg, tail_flags);
        if (none)
            vmm_protect_page(p->pml4, tail_start + o, 0, 1);
    }
    if (moved)
    {
        for (uint64_t o = 0; o < ol; o += PAGE_SIZE)
        {
            uint64_t e = vmm_detach_page(p->pml4, oa + o);
            vmm_map_page(p->pml4, base + o, e & 0x000FFFFFFFFFF000ULL,
                         e & 0xFFFULL & ~(uint64_t)PAGE_PRESENT);
            if (e & PAGE_PROTNONE)
            {
                uint64_t *np = vmm_pte(p->pml4, base + o);
                if (np)
                    *np &= ~(uint64_t)PAGE_PRESENT;
            }
        }
        vma_cut(p, oa, oa + ol);
        vma_add(p, base, base + nl);
    }
    else
        vma_add(p, oa + ol, oa + nl);
    RET(base);
}

uint64_t sys_membarrier(uint64_t *regs)
{
    uint64_t cmd = regs[9];
    if (cmd == 0)
        RET(1);
    if (cmd == 1)
        RET(0);
    RET(-EINVAL);
}
