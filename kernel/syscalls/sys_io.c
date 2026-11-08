#include "syscalls.h"
#include "process.h"
#include "vmm.h"
#include "syscall.h"
#include "kfile.h"
#include "uaccess.h"
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "tty.h"

typedef struct { uint64_t base; uint64_t len; } iovec_t;

static uint64_t vec_io(uint64_t *regs, uint64_t nr)
{
    process_t *p = get_current_process();
    uint64_t fd = regs[9], iov = regs[10], cnt = regs[11];
    if (cnt > 1024)
    {
        regs[14] = (uint64_t)-EINVAL;
        return (uint64_t)regs;
    }
    int64_t total = 0;
    uint64_t orig_nr = regs[14];
    for (uint64_t i = 0; i < cnt; i++)
    {
        uint64_t phys = vmm_virt_to_phys(p->pml4, iov + i * sizeof(iovec_t));
        uint64_t phys2 = vmm_virt_to_phys(p->pml4,
                         iov + i * sizeof(iovec_t) + sizeof(iovec_t) - 1);
        if (!phys || !phys2)
        {
            regs[14] = total ? (uint64_t)total : (uint64_t)-EFAULT;
            return (uint64_t)regs;
        }
        iovec_t v = *(iovec_t *)phys;
        if (v.len == 0)
            continue;
        void *chan = 0;
        int64_t r = kfd_rw(p, (int64_t)fd, nr == 1, v.base, v.len, &chan);
        if (r == E_BLOCK)
        {
            if (total)
                break;
            return sleep_on(regs, orig_nr, chan);
        }
        if (r < 0)
        {
            if (!total)
                total = r;
            break;
        }
        total += r;
        if ((uint64_t)r < v.len)
            break;
    }
    regs[14] = (uint64_t)total;
    return (uint64_t)regs;
}

uint64_t sys_writev(uint64_t *regs) { return vec_io(regs, 1); }
uint64_t sys_readv(uint64_t *regs)  { return vec_io(regs, 0); }

#define FIONREAD 0x541B
#define FIONBIO  0x5421
#define FIOCLEX  0x5451
#define FIONCLEX 0x5450

uint64_t sys_ioctl(uint64_t *regs)
{
    process_t *p = get_current_process();
    int64_t fd = (int64_t)regs[9];
    uint64_t req = regs[10], arg = regs[11];
    struct kfile *f = fd_get(p, fd);
    if (!f)
    {
        regs[14] = (uint64_t)-EBADF;
        return (uint64_t)regs;
    }
    int64_t r = -ENOTTY;
    switch (req)
    {
    case FIONREAD:
    {
        int n = 0;
        if (f->type == FT_PIPE_R)
            n = pipe_nread(f);
        else if (f->type == FT_CONSOLE)
            n = tty_nread();
        else if (f->type == FT_INODE)
        {
            n = (int)(f->ip->size > f->off ? f->ip->size - f->off : 0);
        }
        r = copy_to_user(p, arg, &n, sizeof n) < 0 ? -EFAULT : 0;
        break;
    }
    case FIONBIO:
    {
        int v;
        if (copy_from_user(p, &v, arg, sizeof v) < 0)
            r = -EFAULT;
        else
        {
            f->flags = v ? (f->flags | O_NONBLOCK) : (f->flags & ~O_NONBLOCK);
            r = 0;
        }
        break;
    }
    case FIOCLEX:  p->fd_flags[fd] |= FD_CLOEXEC; r = 0; break;
    case FIONCLEX: p->fd_flags[fd] &= ~FD_CLOEXEC; r = 0; break;
    default:
        if (f->type == FT_CONSOLE)
            r = tty_ioctl(p, f, req, arg);
        break;
    }
    regs[14] = (uint64_t)r;
    return (uint64_t)regs;
}
