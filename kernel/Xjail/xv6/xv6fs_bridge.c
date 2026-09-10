#include "types.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "defs.h"
#include "console.h"   
#include "ahci/ahci.h" 
#define BRIDGE_ERR_COLOR 0x00FF0000
#define BRIDGE_OK_COLOR  0x00000000
static void
bridge_halt(const char *msg)
{
    kprint("[xv6fs_bridge] FATAL: ", BRIDGE_ERR_COLOR);
    kprint(msg, BRIDGE_ERR_COLOR);
    kprint("\n", BRIDGE_ERR_COLOR);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
static ahci_device_t *ahci_dev = 0;
void
xv6fs_bridge_init(ahci_device_t *dev)
{
    ahci_dev = dev;
}
#ifndef XV6FS_NBUF
#define XV6FS_NBUF 64
#endif
static struct buf bcache_pool[XV6FS_NBUF];
static int        bcache_inited = 0;
static int        bcache_next_victim = 0;
static void
bcache_init(void)
{
    int i;
    for (i = 0; i < XV6FS_NBUF; i++) {
        bcache_pool[i].flags   = 0;
        bcache_pool[i].dev     = (uint)-1;
        bcache_pool[i].blockno = 0;
        bcache_pool[i].prev = bcache_pool[i].next = bcache_pool[i].qnext = 0;
    }
    bcache_inited = 1;
}
static struct buf *
bcache_get(uint dev, uint blockno)
{
    int i;
    if (!bcache_inited)
        bcache_init();
    for (i = 0; i < XV6FS_NBUF; i++) {
        struct buf *b = &bcache_pool[i];
        if ((b->flags & B_VALID) && b->dev == dev && b->blockno == blockno)
            return b;
    }
    struct buf *victim = &bcache_pool[bcache_next_victim];
    bcache_next_victim = (bcache_next_victim + 1) % XV6FS_NBUF;
    if ((victim->flags & B_VALID) && (victim->flags & B_DIRTY)) {
        if (ahci_dev) {
            ahci_write_sectors(ahci_dev, victim->blockno, 1, victim->data);
        }
    }
    victim->dev     = dev;
    victim->blockno = blockno;
    victim->flags   = 0;
    return victim;
}
struct buf *
bread(int dev, uint32_t blockno)
{
    struct buf *b = bcache_get((uint)dev, (uint)blockno);
    if (!(b->flags & B_VALID)) {
        if (ahci_dev == 0)
            bridge_halt("bread() - AHCI device not initialized");
        if (ahci_read_sectors(ahci_dev, (uint64_t)blockno, 1, b->data) != 0)
            bridge_halt("bread() - AHCI read error");
        b->flags |= B_VALID;
        b->flags &= ~B_DIRTY;
    }
    return b;
}
void
brelse(struct buf *bp)
{
    (void)bp;
}
void
bwrite(struct buf *b)
{
    if (!b || !ahci_dev) return;
    if (ahci_write_sectors(ahci_dev, (uint64_t)b->blockno, 1, b->data) != 0) {
        bridge_halt("bwrite() - AHCI write error");
    }
    b->flags &= ~B_DIRTY;
}
void
log_write(struct buf *bp)
{
    bp->flags |= B_DIRTY;
    bwrite(bp);
}
void
begin_op(void) {  }
void
end_op(void) {  }
struct inode *namei(char *path);
void          iput(struct inode *ip);
static struct proc xv6fs_fake_proc;
static int          xv6fs_fake_proc_inited = 0;
struct proc *
myproc(void)
{
    if (!xv6fs_fake_proc_inited) {
        xv6fs_fake_proc.cwd = namei("/");
        if (xv6fs_fake_proc.cwd == 0)
            bridge_halt("root inode not found");
        xv6fs_fake_proc_inited = 1;
    }
    return &xv6fs_fake_proc;
}
void
xv6fs_bridge_set_cwd(struct inode *ip)
{
    struct proc *p = myproc();
    if (p->cwd)
        iput(p->cwd);
    p->cwd = ip;
}
void
cprintf(const char *fmt, ...)
{
    kprint((char *)fmt, BRIDGE_OK_COLOR);
}
#ifndef CONSOLE
#define CONSOLE 1
#endif
static int
console_write(struct inode *ip, char *buf, int n)
{
    (void)ip;
    int i;
    for (i = 0; i < n; i++) {
        char c[2] = { buf[i], 0 };
        kprint(c, BRIDGE_OK_COLOR);
    }
    return n;
}
static int
console_read(struct inode *ip, char *buf, int n)
{
    (void)ip; (void)buf; (void)n;
    return 0;
}
struct devsw devsw[NDEV];
static void
devsw_init(void)
{
    devsw[CONSOLE].read  = console_read;
    devsw[CONSOLE].write = console_write;
}
void
initlock(struct spinlock *lk, const char *name)
{
    lk->locked = 0;
    lk->name = name;
}
void
acquire(struct spinlock *lk)
{
    if (lk->locked)
        bridge_halt("acquire()");
    lk->locked = 1;
}
void
release(struct spinlock *lk)
{
    if (!lk->locked)
        bridge_halt("release()");
    lk->locked = 0;
}
int
holding(struct spinlock *lk)
{
    return lk->locked;
}
void
initsleeplock(struct sleeplock *lk, const char *name)
{
    lk->locked = 0;
    lk->name = name;
    lk->pid = 0;
    initlock(&lk->lk, "sleeplock guard");
}
void
acquiresleep(struct sleeplock *lk)
{
    if (lk->locked)
        bridge_halt("acquiresleep(struct sleeplock *lk)");
    lk->locked = 1;
    lk->pid = 0;
}
void
releasesleep(struct sleeplock *lk)
{
    if (!lk->locked)
        bridge_halt("    if (!lk->locked)");
    lk->locked = 0;
}
int
holdingsleep(struct sleeplock *lk)
{
    return lk->locked;
}
void          ilock(struct inode *ip);
void          iunlock(struct inode *ip);
int           readi(struct inode *ip, char *dst, uint off, uint n);
int
xv6fs_bridge_read_file(const char *path, uint8_t *out, uint maxlen)
{
    struct inode *ip;
    int n;
    begin_op();
    ip = namei((char *)path);
    if (ip == 0) {
        end_op();
        kprint("[xv6fsbridge] file not found or u real stupid: ", BRIDGE_ERR_COLOR);
        kprint(path, BRIDGE_ERR_COLOR);
        kprint("\n", BRIDGE_ERR_COLOR);
        return -1;
    }
    ilock(ip);
    n = readi(ip, (char *)out, 0, maxlen);
    iunlock(ip);
    iput(ip);
    end_op();
    return n;
}
void xv6fs_iinit(void);
void
xv6fs_full_init(ahci_device_t *dev)
{
    xv6fs_bridge_init(dev);
    devsw_init();
    xv6fs_iinit();
}
