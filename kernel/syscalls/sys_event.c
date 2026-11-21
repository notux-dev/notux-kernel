#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "kfile.h"
#include "tty.h"
#include "numbers.h"
#include "ktime.h"
#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)
#define EFD_SEMAPHORE 1
#define EFD_NONBLOCK 04000
#define EFD_CLOEXEC 02000000
#define TFD_TIMER_ABSTIME 1
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#define EP_ET (1u << 31)
#define EP_ONESHOT (1u << 30)
#define EP_MAX 64
#define NEPOLL 16
#define TICK_NS (1000000000ULL / HZ)
char sigfd_chan, timerfd_chan;
static struct kfile *new_file(process_t *p, int type, int flags, int *fdout)
{
    struct kfile *f = kfile_alloc(type, 2 | ((flags & EFD_NONBLOCK) ? O_NONBLOCK : 0), 0);
    if (!f)
    {
        *fdout = -ENFILE;
        return 0;
    }
    int fd = fd_install(p, f, 0, (flags & EFD_CLOEXEC) != 0);
    if (fd < 0)
    {
        kfile_put(f);
        *fdout = fd;
        return 0;
    }
    *fdout = fd;
    return f;
}
uint64_t sys_eventfd2(uint64_t *regs)
{
    process_t *p = get_current_process();
    int flags = (int)regs[10];
    if (flags & ~(EFD_SEMAPHORE | EFD_NONBLOCK | EFD_CLOEXEC))
        RET(-EINVAL);
    int fd;
    struct kfile *f = new_file(p, FT_EVENT, flags, &fd);
    if (!f)
        RET(fd);
    f->ev_a = (uint32_t)regs[9];
    f->ev_flag = flags & EFD_SEMAPHORE;
    RET(fd);
}
uint64_t sys_eventfd(uint64_t *regs)
{
    regs[10] = 0;
    return sys_eventfd2(regs);
}
#define NARMED 64
static struct kfile *armed[NARMED];
static void arm_reg(struct kfile *f, int on)
{
    for (int i = 0; i < NARMED; i++)
        if (armed[i] == f)
            armed[i] = on ? f : 0;
    if (on)
        for (int i = 0; i < NARMED; i++)
            if (!armed[i]) { armed[i] = f; return; }
}
void timerfd_tick(void)
{
    for (int i = 0; i < NARMED; i++)
    {
        struct kfile *f = armed[i];
        if (f && f->ev_a && !f->ev_flag && ktime_ticks >= f->ev_a)
        {
            f->ev_flag = 1;
            wakeup(&timerfd_chan);
            wakeup(&poll_chan);
        }
    }
}
uint64_t sys_timerfd_create(uint64_t *regs)
{
    process_t *p = get_current_process();
    int clock = (int)regs[9], flags = (int)regs[10];
    if (flags & ~(EFD_NONBLOCK | EFD_CLOEXEC))
        RET(-EINVAL);
    int ok;
    clock_ns(clock, &ok);
    if (!ok)
        RET(-EINVAL);
    int fd;
    struct kfile *f = new_file(p, FT_TIMER, flags, &fd);
    if (!f)
        RET(fd);
    f->pkind = clock;
    RET(fd);
}
struct itspec { int64_t isec, insec, vsec, vnsec; };
static uint64_t ns_to_ticks(uint64_t ns)
{
    uint64_t t = (ns + TICK_NS - 1) / TICK_NS;
    return t ? t : 1;
}
static struct kfile *timer_of(process_t *p, int64_t fd)
{
    struct kfile *f = fd_get(p, fd);
    return f && f->type == FT_TIMER ? f : 0;
}
static void timer_get(struct kfile *f, struct itspec *o)
{
    uint64_t rem = f->ev_a && f->ev_a > ktime_ticks ? (f->ev_a - ktime_ticks) * TICK_NS : 0;
    if (f->ev_a && !rem)
        rem = 1;
    uint64_t iv = f->ev_b * TICK_NS;
    o->isec = (int64_t)(iv / 1000000000ULL); o->insec = (int64_t)(iv % 1000000000ULL);
    o->vsec = (int64_t)(rem / 1000000000ULL); o->vnsec = (int64_t)(rem % 1000000000ULL);
}
uint64_t sys_timerfd_settime(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct kfile *f = timer_of(p, (int64_t)regs[9]);
    if (!f)
        RET(-EBADF);
    int flags = (int)regs[10];
    struct itspec n;
    if (flags & ~TFD_TIMER_ABSTIME)
        RET(-EINVAL);
    if (copy_from_user(p, &n, regs[11], sizeof n) < 0)
        RET(-EFAULT);
    if (n.isec < 0 || n.vsec < 0 || n.insec < 0 || n.insec >= 1000000000 || n.vnsec < 0 || n.vnsec >= 1000000000)
        RET(-EINVAL);
    if (regs[5])
    {
        struct itspec old;
        timer_get(f, &old);
        if (copy_to_user(p, regs[5], &old, sizeof old) < 0)
            RET(-EFAULT);
    }
    uint64_t value = (uint64_t)n.vsec * 1000000000ULL + (uint64_t)n.vnsec;
    uint64_t interval = (uint64_t)n.isec * 1000000000ULL + (uint64_t)n.insec;
    f->ev_flag = 0;
    f->ev_b = interval ? ns_to_ticks(interval) : 0;
    if (!value)
    {
        f->ev_a = 0;
        arm_reg(f, 0);
        RET(0);
    }
    if (flags & TFD_TIMER_ABSTIME)
    {
        int ok;
        uint64_t now = clock_ns(f->pkind, &ok);
        value = value > now ? value - now : 0;
    }
    f->ev_a = ktime_ticks + (value ? ns_to_ticks(value) : 0);
    if (f->ev_a <= ktime_ticks)
        f->ev_a = ktime_ticks;
    if (!f->ev_a)
        f->ev_a = 1;
    arm_reg(f, 1);
    wakeup(&poll_chan);
    RET(0);
}
uint64_t sys_timerfd_gettime(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct kfile *f = timer_of(p, (int64_t)regs[9]);
    if (!f)
        RET(-EBADF);
    struct itspec cur;
    timer_get(f, &cur);
    if (copy_to_user(p, regs[10], &cur, sizeof cur) < 0)
        RET(-EFAULT);
    RET(0);
}
static uint64_t sigfd_mask(uint64_t m)
{
    return m & ~((1ULL << 8) | (1ULL << 18));
}
static uint64_t do_signalfd(uint64_t *regs, int flags)
{
    process_t *p = get_current_process();
    int64_t fd = (int64_t)(int32_t)regs[9];
    uint64_t mask;
    if (regs[11] != 8)
        RET(-EINVAL);
    if (flags & ~(EFD_NONBLOCK | EFD_CLOEXEC))
        RET(-EINVAL);
    if (copy_from_user(p, &mask, regs[10], 8) < 0)
        RET(-EFAULT);
    if (fd == -1)
    {
        int nfd;
        struct kfile *f = new_file(p, FT_SIGNAL, flags, &nfd);
        if (!f)
            RET(nfd);
        f->ev_a = sigfd_mask(mask);
        RET(nfd);
    }
    struct kfile *f = fd_get(p, fd);
    if (!f)
        RET(-EBADF);
    if (f->type != FT_SIGNAL)
        RET(-EINVAL);
    f->ev_a = sigfd_mask(mask);
    wakeup(&poll_chan);
    RET(fd);
}
uint64_t sys_signalfd4(uint64_t *regs) { return do_signalfd(regs, (int)regs[5]); }
uint64_t sys_signalfd(uint64_t *regs) { return do_signalfd(regs, 0); }
static int timer_due(struct kfile *f) { return f->ev_a && ktime_ticks >= f->ev_a; }
int64_t ev_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    int nb = (f->flags & O_NONBLOCK) != 0;
    if (f->type == FT_EVDEV)
        return evdev_read(p, f, ubuf, n);
    if (f->type == FT_FB)
        return fb_read(p, f, ubuf, n);
    if (f->type == FT_PTM)
        return pty_master_read(p, f, ubuf, n);
    if (f->type == FT_EVENT)
    {
        if (n < 8)
            return -EINVAL;
        if (f->ev_a == 0)
            return nb ? -EAGAIN : E_BLOCK;
        uint64_t v = f->ev_flag ? 1 : f->ev_a;
        if (copy_to_user(p, ubuf, &v, 8) < 0)
            return -EFAULT;
        f->ev_a -= v;
        wakeup(f);
        wakeup(&poll_chan);
        return 8;
    }
    if (f->type == FT_TIMER)
    {
        if (n < 8)
            return -EINVAL;
        if (!timer_due(f))
            return nb ? -EAGAIN : E_BLOCK;
        uint64_t count = 1;
        if (f->ev_b)
            count += (ktime_ticks - f->ev_a) / f->ev_b;
        if (copy_to_user(p, ubuf, &count, 8) < 0)
            return -EFAULT;
        if (f->ev_b)
            f->ev_a += count * f->ev_b;
        else
        {
            f->ev_a = 0;
            arm_reg(f, 0);
        }
        f->ev_flag = 0;
        return 8;
    }
    if (f->type == FT_SIGNAL)
    {
        if (n < 128)
            return -EINVAL;
        uint64_t total = 0;
        while (n - total >= 128 && (p->sigpending & f->ev_a))
        {
            uint64_t ready = p->sigpending & f->ev_a;
            int sig = 1;
            while (!(ready & (1ULL << (sig - 1)))) sig++;
            uint8_t si[128] = {0};
            si[0] = (uint8_t)sig;
            if (copy_to_user(p, ubuf + total, si, 128) < 0)
                return total ? (int64_t)total : -EFAULT;
            p->sigpending &= ~(1ULL << (sig - 1));
            total += 128;
        }
        if (!total)
            return nb ? -EAGAIN : E_BLOCK;
        return (int64_t)total;
    }
    return -EINVAL;
}
int64_t ev_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    if (f->type == FT_EVDEV)
        return evdev_write(p, f, ubuf, n);
    if (f->type == FT_FB)
        return fb_write(p, f, ubuf, n);
    if (f->type == FT_PTM)
        return pty_master_write(p, f, ubuf, n);
    if (f->type != FT_EVENT || n < 8)
        return -EINVAL;
    uint64_t v;
    if (copy_from_user(p, &v, ubuf, 8) < 0)
        return -EFAULT;
    if (v == ~0ULL)
        return -EINVAL;
    if (f->ev_a > 0xFFFFFFFFFFFFFFFEULL - v)
        return (f->flags & O_NONBLOCK) ? -EAGAIN : E_BLOCK;
    f->ev_a += v;
    wakeup(f);
    wakeup(&poll_chan);
    return 8;
}
void *ev_chan(struct kfile *f)
{
    if (f->type == FT_TIMER)
        return &timerfd_chan;
    if (f->type == FT_SIGNAL)
        return &sigfd_chan;
    if (f->type == FT_EVDEV)
        return &input_chan;
    if (f->type == FT_PTM)
        return pty_master_chan(f);
    return f;
}
struct epitem { struct kfile *f; int fd; uint32_t ev; uint64_t data; uint32_t last; int off; };
struct epoll { int used, n; struct epitem it[EP_MAX]; };
static struct epoll eps[NEPOLL];
static int nep;
struct __attribute__((packed)) epev { uint32_t events; uint64_t data; };
void epoll_forget(struct kfile *f)
{
    if (!nep)
        return;
    for (int i = 0; i < NEPOLL; i++)
    {
        struct epoll *e = &eps[i];
        if (!e->used)
            continue;
        for (int k = 0; k < e->n;)
            if (e->it[k].f == f)
                e->it[k] = e->it[--e->n];
            else
                k++;
    }
}
void ev_close(struct kfile *f)
{
    if (f->type == FT_EVDEV)
        evdev_close(f);
    if (f->type == FT_FB)
        fb_close(f);
    if (f->type == FT_PTM)
        pty_master_close(f);
    if (f->type == FT_TIMER)
        arm_reg(f, 0);
    if (f->type == FT_EPOLL && f->obj)
    {
        struct epoll *e = f->obj;
        e->used = 0;
        e->n = 0;
        nep--;
        f->obj = 0;
    }
}
static int ep_scan(struct epoll *e, struct epev *out, int max, int *console_in)
{
    int got = 0;
    for (int i = 0; i < e->n && got < max; i++)
    {
        struct epitem *it = &e->it[i];
        if (it->off)
            continue;
        if (it->f->type == FT_CONSOLE && (it->ev & 1))
            *console_in = 1;
        uint32_t m = (uint32_t)kfile_poll(it->f) & (it->ev | 8 | 0x10);
        uint32_t fresh = (it->ev & EP_ET) ? (m & ~it->last) : m;
        if (it->ev & EP_ET)
            it->last = m;
        if (!fresh)
            continue;
        if (out)
        {
            out[got].events = m;
            out[got].data = it->data;
        }
        got++;
        if (it->ev & EP_ONESHOT)
            it->off = 1;
    }
    return got;
}
int ev_poll(struct kfile *f)
{
    if (f->type == FT_EVDEV)
        return evdev_poll(f);
    if (f->type == FT_FB)
        return 1 | 4;
    if (f->type == FT_PTM)
        return pty_master_poll(f);
    if (f->type == FT_EVENT)
        return (f->ev_a ? 1 : 0) | (f->ev_a < 0xFFFFFFFFFFFFFFFEULL ? 4 : 0);
    if (f->type == FT_TIMER)
        return timer_due(f) ? 1 : 0;
    if (f->type == FT_SIGNAL)
        return (get_current_process()->sigpending & f->ev_a) ? 1 : 0;
    struct epoll *e = f->obj;
    for (int i = 0; e && i < e->n; i++)
        if (!e->it[i].off && ((uint32_t)kfile_poll(e->it[i].f) & (e->it[i].ev | 8 | 0x10)))
            return 1;
    return 0;
}
static uint64_t do_epoll_create(uint64_t *regs, int flags)
{
    process_t *p = get_current_process();
    if (flags & ~EFD_CLOEXEC)
        RET(-EINVAL);
    struct epoll *e = 0;
    for (int i = 0; i < NEPOLL; i++)
        if (!eps[i].used) { e = &eps[i]; break; }
    if (!e)
        RET(-EMFILE);
    int fd;
    struct kfile *f = new_file(p, FT_EPOLL, flags, &fd);
    if (!f)
        RET(fd);
    e->used = 1; e->n = 0;
    f->obj = e;
    nep++;
    RET(fd);
}
uint64_t sys_epoll_create1(uint64_t *regs) { return do_epoll_create(regs, (int)regs[9]); }
uint64_t sys_epoll_create(uint64_t *regs)
{
    if ((int)regs[9] <= 0)
        RET(-EINVAL);
    return do_epoll_create(regs, 0);
}
uint64_t sys_epoll_ctl(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct kfile *ef = fd_get(p, (int64_t)regs[9]);
    int op = (int)regs[10];
    int64_t fd = (int64_t)(int32_t)regs[11];
    struct kfile *tf = fd_get(p, fd);
    if (!ef || !tf)
        RET(-EBADF);
    if (ef->type != FT_EPOLL || ef == tf)
        RET(-EINVAL);
    if (tf->type == FT_INODE)
        RET(-EPERM);
    struct epoll *e = ef->obj;
    struct epev ev = {0, 0};
    if (op != EPOLL_CTL_DEL && copy_from_user(p, &ev, regs[5], sizeof ev) < 0)
        RET(-EFAULT);
    int k = -1;
    for (int i = 0; i < e->n; i++)
        if (e->it[i].f == tf && e->it[i].fd == (int)fd)
            k = i;
    if (op == EPOLL_CTL_ADD)
    {
        if (k >= 0)
            RET(-EEXIST);
        if (e->n == EP_MAX)
            RET(-ENOSPC);
        e->it[e->n++] = (struct epitem){ tf, (int)fd, ev.events, ev.data, 0, 0 };
        wakeup(&poll_chan);
        RET(0);
    }
    if (k < 0)
        RET(-ENOENT);
    if (op == EPOLL_CTL_DEL)
        e->it[k] = e->it[--e->n];
    else if (op == EPOLL_CTL_MOD)
    {
        e->it[k].ev = ev.events; e->it[k].data = ev.data; e->it[k].last = 0; e->it[k].off = 0;
        wakeup(&poll_chan);
    }
    else
        RET(-EINVAL);
    RET(0);
}
static uint64_t do_epoll_wait(uint64_t *regs, uint64_t nr)
{
    process_t *p = get_current_process();
    struct kfile *ef = fd_get(p, (int64_t)regs[9]);
    int max = (int)regs[11];
    int64_t timeout = (int32_t)regs[5];
    if (!ef)
        RET(-EBADF);
    if (ef->type != FT_EPOLL || max <= 0)
        RET(-EINVAL);
    if (max > EP_MAX)
        max = EP_MAX;
    struct epev out[EP_MAX];
    int console_in = 0;
    int n = ep_scan(ef->obj, out, max, &console_in);
    if (n)
    {
        p->tmo_active = 0;
        if (copy_to_user(p, regs[10], out, (uint64_t)n * sizeof(struct epev)) < 0)
            RET(-EFAULT);
        RET(n);
    }
    return poll_wait(regs, nr, timeout < 0 ? -1 : timeout, console_in);
}
uint64_t sys_epoll_wait(uint64_t *regs) { return do_epoll_wait(regs, NR_epoll_wait); }
uint64_t sys_epoll_pwait(uint64_t *regs) { return do_epoll_wait(regs, NR_epoll_pwait); }
