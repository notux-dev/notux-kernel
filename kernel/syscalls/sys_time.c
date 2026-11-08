#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "kfile.h"
#include "ktime.h"


#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)

char poll_chan;

struct kts { int64_t sec, nsec; };
struct ktv { int64_t sec, usec; };

#define NSEC 1000000000LL

static uint64_t clock_ns(int id, int *ok)
{
    *ok = 1;
    switch (id)
    {
    case 0: case 5: return ktime_realtime_ns();
    case 1: case 4: case 6: case 7:
    case 2: case 3: return ktime_monotonic_ns();
    }
    *ok = 0;
    return 0;
}

uint64_t sys_clock_gettime(uint64_t *regs)
{
    int ok;
    uint64_t ns = clock_ns((int)regs[9], &ok);
    if (!ok)
        RET(-EINVAL);
    struct kts t = { (int64_t)(ns / NSEC), (int64_t)(ns % NSEC) };
    if (copy_to_user(get_current_process(), regs[10], &t, sizeof t) < 0)
        RET(-EFAULT);
    RET(0);
}

uint64_t sys_clock_getres(uint64_t *regs)
{
    int ok;
    clock_ns((int)regs[9], &ok);
    if (!ok)
        RET(-EINVAL);
    if (regs[10])
    {
        struct kts t = { 0, NSEC / HZ };
        if (copy_to_user(get_current_process(), regs[10], &t, sizeof t) < 0)
            RET(-EFAULT);
    }
    RET(0);
}

uint64_t sys_gettimeofday(uint64_t *regs)
{
    uint64_t ns = ktime_realtime_ns();
    if (regs[9])
    {
        struct ktv t = { (int64_t)(ns / NSEC), (int64_t)(ns % NSEC / 1000) };
        if (copy_to_user(get_current_process(), regs[9], &t, sizeof t) < 0)
            RET(-EFAULT);
    }
    RET(0);
}

uint64_t sys_time(uint64_t *regs)
{
    int64_t s = (int64_t)(ktime_realtime_ns() / NSEC);
    if (regs[9] && copy_to_user(get_current_process(), regs[9], &s, sizeof s) < 0)
        RET(-EFAULT);
    RET(s);
}

uint64_t sys_times(uint64_t *regs)
{
    if (regs[9])
    {
        int64_t z[4] = {0, 0, 0, 0};
        if (copy_to_user(get_current_process(), regs[9], z, sizeof z) < 0)
            RET(-EFAULT);
    }
    RET((int64_t)ktime_ticks);
}

uint64_t sys_sched_yield(uint64_t *regs)
{
    RET(0);
}

static uint64_t ms_to_ticks(uint64_t ms) { return (ms * HZ + 999) / 1000 ?: 1; }

static int tmo_expired(process_t *p, uint64_t ticks_from_now)
{
    if (!p->tmo_active)
    {
        p->tmo_active = 1;
        p->tmo_deadline = ktime_ticks + ticks_from_now;
    }
    if (ktime_ticks >= p->tmo_deadline)
    {
        p->tmo_active = 0;
        return 1;
    }
    return 0;
}

static uint64_t do_sleep(uint64_t *regs, uint64_t nr, uint64_t ns)
{
    process_t *p = get_current_process();
    if (ns == 0)
        RET(0);
    uint64_t ticks = (ns + (NSEC / HZ) - 1) / (NSEC / HZ);
    if (tmo_expired(p, ticks))
        RET(0);
    return sleep_until(regs, nr, 0, p->tmo_deadline);
}

static int get_ts(process_t *p, uint64_t uptr, uint64_t *ns)
{
    struct kts t;
    if (copy_from_user(p, &t, uptr, sizeof t) < 0)
        return -EFAULT;
    if (t.sec < 0 || t.nsec < 0 || t.nsec >= NSEC)
        return -EINVAL;
    *ns = (uint64_t)t.sec * NSEC + (uint64_t)t.nsec;
    return 0;
}

uint64_t sys_nanosleep(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t ns;
    int e = get_ts(p, regs[9], &ns);
    if (e)
        RET(e);
    return do_sleep(regs, 35, ns);
}

uint64_t sys_rt_sigtimedwait(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t set, ns = 0;
    if (regs[5] != 8)
        RET(-EINVAL);
    if (copy_from_user(p, &set, regs[9], sizeof set) < 0)
        RET(-EFAULT);
    uint64_t hit = p->sigpending & set;
    if (hit)
    {
        int sig = __builtin_ctzll(hit) + 1;
        p->sigpending &= ~(1ULL << (sig - 1));
        p->tmo_active = 0;
        if (regs[10])
        {
            int info[32] = {0};
            info[0] = sig;
            if (copy_to_user(p, regs[10], info, sizeof info) < 0)
                RET(-EFAULT);
        }
        RET(sig);
    }
    if (regs[11])
    {
        int e = get_ts(p, regs[11], &ns);
        if (e)
            RET(e);
        if (ns == 0)
            RET(-EAGAIN);
        uint64_t ticks = (ns + (NSEC / HZ) - 1) / (NSEC / HZ);
        if (tmo_expired(p, ticks))
            RET(-EAGAIN);
    }
    return sleep_until(regs, 128, 0, regs[11] ? p->tmo_deadline : ktime_ticks + 1);
}

uint64_t sys_clock_nanosleep(uint64_t *regs)
{
    process_t *p = get_current_process();
    int ok;
    uint64_t now = clock_ns((int)regs[9], &ok);
    uint64_t ns;
    if (!ok)
        RET(-EINVAL);
    int e = get_ts(p, regs[11], &ns);
    if (e)
        RET(e);
    if (regs[10] & 1)
        ns = ns > now ? ns - now : 0;
    return do_sleep(regs, 230, ns);
}


#define POLLIN 1
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 0x10
#define POLLNVAL 0x20

struct kpollfd { int fd; short events; short revents; };

static uint64_t poll_wait(uint64_t *regs, uint64_t nr, int64_t timeout_ms, int console_in)
{
    process_t *p = get_current_process();
    uint64_t wake = 0;
    if (timeout_ms == 0)
    {
        p->tmo_active = 0;
        RET(0);
    }
    if (timeout_ms > 0)
    {
        if (tmo_expired(p, ms_to_ticks((uint64_t)timeout_ms)))
            RET(0);
        wake = p->tmo_deadline;
    }
    if (console_in && (!wake || wake > ktime_ticks + 1))
        wake = ktime_ticks + 1;
    return sleep_until(regs, nr, &poll_chan, wake);
}

static uint64_t do_poll(uint64_t *regs, uint64_t nr, uint64_t ufds, uint64_t nfds,
                        int64_t timeout_ms)
{
    process_t *p = get_current_process();
    if (nfds > MAX_FDS * 4)
        RET(-EINVAL);
    struct kpollfd fds[MAX_FDS * 4];
    if (nfds && copy_from_user(p, fds, ufds, nfds * sizeof(struct kpollfd)) < 0)
        RET(-EFAULT);
    int ready = 0, console_in = 0;
    for (uint64_t i = 0; i < nfds; i++)
    {
        fds[i].revents = 0;
        if (fds[i].fd < 0)
            continue;
        int m = kfd_poll(p, fds[i].fd);
        if (m & POLLNVAL)
            fds[i].revents = POLLNVAL;
        else
        {
            fds[i].revents = (short)(m & (fds[i].events | POLLERR | POLLHUP));
            struct kfile *f = fd_get(p, fds[i].fd);
            if (f && f->type == FT_CONSOLE && (fds[i].events & POLLIN))
                console_in = 1;
        }
        if (fds[i].revents)
            ready++;
    }
    if (ready)
    {
        p->tmo_active = 0;
        if (nfds && copy_to_user(p, ufds, fds, nfds * sizeof(struct kpollfd)) < 0)
            RET(-EFAULT);
        RET(ready);
    }
    return poll_wait(regs, nr, timeout_ms, console_in);
}

uint64_t sys_poll(uint64_t *regs)
{
    int64_t t = (int32_t)regs[11];
    return do_poll(regs, 7, regs[9], regs[10], t < 0 ? -1 : t);
}

uint64_t sys_ppoll(uint64_t *regs)
{
    int64_t t = -1;
    if (regs[11])
    {
        uint64_t ns;
        int e = get_ts(get_current_process(), regs[11], &ns);
        if (e)
            RET(e);
        t = (int64_t)((ns + 999999) / 1000000);
        if (t == 0 && ns)
            t = 1;
    }
    return do_poll(regs, 271, regs[9], regs[10], t);
}

static uint64_t do_select(uint64_t *regs, uint64_t nr, int64_t timeout_ms)
{
    process_t *p = get_current_process();
    int64_t nfds = (int64_t)regs[9];
    uint64_t usr[3] = { regs[10], regs[11], regs[5] };
    if (nfds < 0 || nfds > 1024)
        RET(-EINVAL);
    uint64_t nwords = ((uint64_t)nfds + 63) / 64;
    uint64_t in[3][16], out[3][16];
    for (int k = 0; k < 3; k++)
    {
        for (int i = 0; i < 16; i++)
            in[k][i] = out[k][i] = 0;
        if (usr[k] && nwords &&
            copy_from_user(p, in[k], usr[k], nwords * 8) < 0)
            RET(-EFAULT);
    }
    int ready = 0, console_in = 0;
    for (int64_t fd = 0; fd < nfds; fd++)
    {
        uint64_t bit = 1ULL << (fd % 64);
        int w = fd / 64;
        int r = (in[0][w] & bit) != 0, wr = (in[1][w] & bit) != 0, ex = (in[2][w] & bit) != 0;
        if (!r && !wr && !ex)
            continue;
        int m = kfd_poll(p, fd);
        if (m & POLLNVAL)
            RET(-EBADF);
        if (r && (m & (POLLIN | POLLHUP | POLLERR)))
        {
            out[0][w] |= bit;
            ready++;
        }
        if (wr && (m & (POLLOUT | POLLERR)))
        {
            out[1][w] |= bit;
            ready++;
        }
        struct kfile *f = fd_get(p, fd);
        if (r && f && f->type == FT_CONSOLE)
            console_in = 1;
    }
    if (ready)
    {
        p->tmo_active = 0;
        for (int k = 0; k < 3; k++)
            if (usr[k] && nwords &&
                copy_to_user(p, usr[k], out[k], nwords * 8) < 0)
                RET(-EFAULT);
        RET(ready);
    }
    uint64_t r = poll_wait(regs, nr, timeout_ms, console_in);
    if (r == (uint64_t)regs && regs[14] == 0)
    {
        for (int k = 0; k < 3; k++)
            if (usr[k] && nwords)
                copy_to_user(p, usr[k], out[k], nwords * 8);
    }
    return r;
}

uint64_t sys_select(uint64_t *regs)
{
    int64_t t = -1;
    if (regs[7])
    {
        struct ktv v;
        if (copy_from_user(get_current_process(), &v, regs[7], sizeof v) < 0)
            RET(-EFAULT);
        if (v.sec < 0 || v.usec < 0 || v.usec >= 1000000)
            RET(-EINVAL);
        t = v.sec * 1000 + (v.usec + 999) / 1000;
    }
    return do_select(regs, 23, t);
}

uint64_t sys_pselect6(uint64_t *regs)
{
    int64_t t = -1;
    if (regs[7])
    {
        uint64_t ns;
        int e = get_ts(get_current_process(), regs[7], &ns);
        if (e)
            RET(e);
        t = (int64_t)((ns + 999999) / 1000000);
        if (t == 0 && ns)
            t = 1;
    }
    return do_select(regs, 270, t);
}
