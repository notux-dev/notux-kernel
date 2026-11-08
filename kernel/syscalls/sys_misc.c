#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "ktime.h"


#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)

struct krlimit { uint64_t cur, max; };

static int64_t rlimit_op(process_t *p, int res, uint64_t newp, uint64_t oldp)
{
    if (res < 0 || res >= 16)
        return -EINVAL;
    struct krlimit old = { p->rlim[res][0], p->rlim[res][1] };
    struct krlimit n;
    if (newp)
    {
        if (copy_from_user(p, &n, newp, sizeof n) < 0)
            return -EFAULT;
        if (n.cur > n.max)
            return -EINVAL;
    }
    if (oldp && copy_to_user(p, oldp, &old, sizeof old) < 0)
        return -EFAULT;
    if (newp)
    {
        p->rlim[res][0] = n.cur;
        p->rlim[res][1] = n.max;
    }
    return 0;
}

uint64_t sys_getrlimit(uint64_t *regs)
{
    RET(rlimit_op(get_current_process(), (int)regs[9], 0, regs[10]));
}
uint64_t sys_setrlimit(uint64_t *regs)
{
    RET(rlimit_op(get_current_process(), (int)regs[9], regs[10], 0));
}
uint64_t sys_prlimit64(uint64_t *regs)
{
    process_t *p = get_current_process();
    if (regs[9] != 0 && regs[9] != p->pid)
        RET(-ESRCH);
    RET(rlimit_op(p, (int)regs[10], regs[11], regs[5]));
}

uint64_t sys_getrusage(uint64_t *regs)
{
    int who = (int)regs[9];
    if (who != 0 && who != -1 && who != 1)
        RET(-EINVAL);
    uint8_t z[144];
    for (int i = 0; i < 144; i++)
        z[i] = 0;
    if (copy_to_user(get_current_process(), regs[10], z, sizeof z) < 0)
        RET(-EFAULT);
    RET(0);
}

static uint64_t rng_state;

static int have_rdrand(void)
{
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return (c >> 30) & 1;
}

static uint64_t next_random(void)
{
    static int checked, rd;
    if (!checked)
    {
        rd = have_rdrand();
        checked = 1;
    }
    if (rd)
    {
        for (int i = 0; i < 10; i++)
        {
            uint64_t v;
            unsigned char ok;
            __asm__ volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
            if (ok)
                return v;
        }
    }
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    rng_state += (((uint64_t)hi << 32) | lo) ^ (ktime_ticks * 0x9E3779B97F4A7C15ULL);
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

uint64_t krandom_u64(void) { return next_random(); }

uint64_t sys_getrandom(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t ubuf = regs[9], len = regs[10], flags = regs[11];
    if (flags & ~7ULL)
        RET(-EINVAL);
    if (len > 33554431)
        len = 33554431;
    uint64_t done = 0;
    while (done < len)
    {
        uint64_t v[32];
        uint64_t n = len - done;
        if (n > sizeof v)
            n = sizeof v;
        for (unsigned i = 0; i < (n + 7) / 8; i++)
            v[i] = next_random();
        if (copy_to_user(p, ubuf + done, v, n) < 0)
            RET(done ? (int64_t)done : -EFAULT);
        done += n;
    }
    RET(done);
}

uint64_t sys_ret0(uint64_t *regs) { RET(0); }

uint64_t sys_getgroups(uint64_t *regs)
{
    RET(0);
}

uint64_t sys_getresid(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint32_t z = 0;
    for (int i = 0; i < 3; i++)
        if (regs[9 + i] && copy_to_user(p, regs[9 + i], &z, sizeof z) < 0)
            RET(-EFAULT);
    RET(0);
}

uint64_t sys_sched_getaffinity(uint64_t *regs)
{
    uint64_t size = regs[10];
    if (size < 8)
        RET(-EINVAL);
    uint64_t m = 1;
    if (copy_to_user(get_current_process(), regs[11], &m, sizeof m) < 0)
        RET(-EFAULT);
    RET(8);
}

#define TICK_US (1000000ULL / 100)
static uint64_t us_to_ticks(uint64_t us) { return (us + TICK_US - 1) / TICK_US; }

uint64_t sys_alarm(uint64_t *regs)
{
    process_t *p = get_current_process();
    extern volatile uint64_t ktime_ticks;
    uint64_t left = p->alarm_tick > ktime_ticks ? p->alarm_tick - ktime_ticks : 0;
    uint64_t secs = (left + 99) / 100;
    p->alarm_interval = 0;
    p->alarm_tick = regs[9] ? ktime_ticks + regs[9] * 100 : 0;
    RET(secs);
}

uint64_t sys_itimer(uint64_t *regs)
{
    process_t *p = get_current_process();
    extern volatile uint64_t ktime_ticks;
    int is_set = regs[14] == 38;
    uint64_t which = regs[9];
    uint64_t dst = is_set ? regs[11] : regs[10];
    if (which > 2)
        RET(-EINVAL);
    if (which != 0)
    {
        uint64_t z[4] = {0, 0, 0, 0};
        if (dst && copy_to_user(p, dst, z, sizeof z) < 0)
            RET(-EFAULT);
        RET(0);
    }
    uint64_t left = p->alarm_tick > ktime_ticks ? p->alarm_tick - ktime_ticks : 0;
    uint64_t cur[4] = { p->alarm_interval / 100, (p->alarm_interval % 100) * TICK_US,
                        left / 100, (left % 100) * TICK_US };
    uint64_t nv[4];
    if (is_set)
    {
        if (!regs[10] || copy_from_user(p, nv, regs[10], sizeof nv) < 0)
            RET(-EFAULT);
    }
    if (dst && copy_to_user(p, dst, cur, sizeof cur) < 0)
        RET(-EFAULT);
    if (is_set)
    {
        uint64_t val = nv[2] * 100 + us_to_ticks(nv[3]);
        p->alarm_interval = nv[0] * 100 + us_to_ticks(nv[1]);
        p->alarm_tick = val ? ktime_ticks + val : 0;
    }
    RET(0);
}
