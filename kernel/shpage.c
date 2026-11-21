#include "shpage.h"
#include "pmm.h"
#include "vmm.h"
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "Xjail/xv6/xv6fs_api.h"
#define NSHP 16384
extern struct inode *idup(struct inode *ip);
struct shp
{
    uint64_t phys;
    uint32_t refs;
    uint8_t state;
    struct inode *ip;
    uint64_t off;
};
#define TAB_PAGES ((NSHP * sizeof(struct shp) + PAGE_SIZE - 1) / PAGE_SIZE)
static struct shp *tab;
static int tab_init(void)
{
    if (tab)
        return 1;
    tab = pmm_alloc_contig((uint32_t)TAB_PAGES);
    if (!tab)
        return 0;
    for (uint64_t i = 0; i < TAB_PAGES * PAGE_SIZE / 8; i++)
        ((uint64_t *)tab)[i] = 0;
    return 1;
}
static uint32_t hash(uint64_t phys)
{
    return (uint32_t)(((phys >> 12) * 2654435761ULL) >> 7) & (NSHP - 1);
}
static struct shp *find(uint64_t phys)
{
    if (!tab)
        return 0;
    uint32_t h = hash(phys);
    for (uint32_t n = 0; n < NSHP; n++)
    {
        struct shp *e = &tab[(h + n) & (NSHP - 1)];
        if (e->state == 0)
            return 0;
        if (e->state == 1 && e->phys == phys)
            return e;
    }
    return 0;
}
static struct shp *insert(uint64_t phys)
{
    uint32_t h = hash(phys);
    for (uint32_t n = 0; n < NSHP; n++)
    {
        struct shp *e = &tab[(h + n) & (NSHP - 1)];
        if (e->state != 1)
        {
            e->phys = phys;
            e->refs = 1;
            e->state = 1;
            e->ip = 0;
            e->off = 0;
            return e;
        }
    }
    return 0;
}
void *shp_anon(void)
{
    if (!tab_init())
        return 0;
    void *pg = pmm_alloc_page();
    if (!pg)
        return 0;
    if (!insert((uint64_t)pg))
    {
        pmm_free_page(pg);
        return 0;
    }
    page_zero(pg);
    return pg;
}
void *shp_file(struct inode *ip, uint64_t off)
{
    if (!tab_init())
        return 0;
    for (uint32_t i = 0; i < NSHP; i++)
        if (tab[i].state == 1 && tab[i].ip == ip && tab[i].off == off)
        {
            tab[i].refs++;
            return (void *)tab[i].phys;
        }
    void *pg = shp_anon();
    if (!pg)
        return 0;
    struct shp *e = find((uint64_t)pg);
    e->ip = idup(ip);
    e->off = off;
    ilock(ip);
    readi(ip, (char *)pg, (uint)off, PAGE_SIZE);
    iunlock(ip);
    return pg;
}
void shp_ref(uint64_t phys)
{
    struct shp *e = find(phys);
    if (e)
        e->refs++;
}
void shp_sync(uint64_t phys)
{
    struct shp *e = find(phys);
    if (!e || !e->ip)
        return;
    ilock(e->ip);
    uint64_t size = e->ip->size;
    if (e->off < size)
    {
        uint64_t n = size - e->off;
        if (n > PAGE_SIZE)
            n = PAGE_SIZE;
        writei(e->ip, (char *)phys, (uint)e->off, (uint)n);
    }
    iunlock(e->ip);
}
void shp_unref(uint64_t phys)
{
    struct shp *e = find(phys);
    if (!e)
    {
        pmm_free_page((void *)phys);
        return;
    }
    if (--e->refs > 0)
        return;
    if (e->ip)
    {
        shp_sync(phys);
        iput(e->ip);
    }
    e->state = 2;
    pmm_free_page((void *)phys);
}
