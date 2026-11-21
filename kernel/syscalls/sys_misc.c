#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "ktime.h"
#include "perm.h"
#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)
struct krlimit { uint64_t cur, max; };
static int64_t rlimit_op(process_t *p, int res, uint64_t newp, uint64_t oldp)
{
    if (res < 0 || res >= 16)
        return -EINVAL;
    struct krlimit old = { p->sh->rlim[res][0], p->sh->rlim[res][1] };
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
        p->sh->rlim[res][0] = n.cur;
        p->sh->rlim[res][1] = n.max;
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
    if (regs[9] != 0 && regs[9] != p->tgid && regs[9] != p->pid)
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
uint64_t sys_getuid_(uint64_t *regs) { RET(get_current_process()->cred.uid); }
uint64_t sys_geteuid_(uint64_t *regs) { RET(get_current_process()->cred.euid); }
uint64_t sys_getgid_(uint64_t *regs) { RET(get_current_process()->cred.gid); }
uint64_t sys_getegid_(uint64_t *regs) { RET(get_current_process()->cred.egid); }
#define NOID 0xFFFFFFFFu
uint64_t sys_setuid(uint64_t *regs)
{
    cred_t *c = &get_current_process()->cred;
    uint32_t u = (uint32_t)regs[9];
    if (u == NOID)
        RET(-EINVAL);
    if (c->euid == 0)
        c->uid = c->euid = c->suid = u;
    else if (u == c->uid || u == c->suid)
        c->euid = u;
    else
        RET(-EPERM);
    RET(0);
}
uint64_t sys_setgid(uint64_t *regs)
{
    cred_t *c = &get_current_process()->cred;
    uint32_t g = (uint32_t)regs[9];
    if (g == NOID)
        RET(-EINVAL);
    if (c->euid == 0)
        c->gid = c->egid = c->sgid = g;
    else if (g == c->gid || g == c->sgid)
        c->egid = g;
    else
        RET(-EPERM);
    RET(0);
}
static int id_ok(uint32_t v, uint32_t a, uint32_t b, uint32_t c)
{
    return v == NOID || v == a || v == b || v == c;
}
uint64_t sys_setresuid(uint64_t *regs)
{
    cred_t *c = &get_current_process()->cred;
    uint32_t r = (uint32_t)regs[9], e = (uint32_t)regs[10], s = (uint32_t)regs[11];
    if (c->euid != 0 && !(id_ok(r, c->uid, c->euid, c->suid) && id_ok(e, c->uid, c->euid, c->suid) &&
                          id_ok(s, c->uid, c->euid, c->suid)))
        RET(-EPERM);
    if (r != NOID) c->uid = r;
    if (e != NOID) c->euid = e;
    if (s != NOID) c->suid = s;
    RET(0);
}
uint64_t sys_setresgid(uint64_t *regs)
{
    cred_t *c = &get_current_process()->cred;
    uint32_t r = (uint32_t)regs[9], e = (uint32_t)regs[10], s = (uint32_t)regs[11];
    if (c->euid != 0 && !(id_ok(r, c->gid, c->egid, c->sgid) && id_ok(e, c->gid, c->egid, c->sgid) &&
                          id_ok(s, c->gid, c->egid, c->sgid)))
        RET(-EPERM);
    if (r != NOID) c->gid = r;
    if (e != NOID) c->egid = e;
    if (s != NOID) c->sgid = s;
    RET(0);
}
uint64_t sys_setreuid(uint64_t *regs)
{
    cred_t *c = &get_current_process()->cred;
    uint32_t r = (uint32_t)regs[9], e = (uint32_t)regs[10];
    uint32_t old = c->uid;
    if (c->euid != 0)
    {
        if (r != NOID && r != c->uid && r != c->euid) RET(-EPERM);
        if (e != NOID && e != c->uid && e != c->euid && e != c->suid) RET(-EPERM);
    }
    if (r != NOID) c->uid = r;
    if (e != NOID) c->euid = e;
    if (r != NOID || (e != NOID && e != old))
        c->suid = c->euid;
    RET(0);
}
uint64_t sys_setregid(uint64_t *regs)
{
    cred_t *c = &get_current_process()->cred;
    uint32_t r = (uint32_t)regs[9], e = (uint32_t)regs[10];
    uint32_t old = c->gid;
    if (c->euid != 0)
    {
        if (r != NOID && r != c->gid && r != c->egid) RET(-EPERM);
        if (e != NOID && e != c->gid && e != c->egid && e != c->sgid) RET(-EPERM);
    }
    if (r != NOID) c->gid = r;
    if (e != NOID) c->egid = e;
    if (r != NOID || (e != NOID && e != old))
        c->sgid = c->egid;
    RET(0);
}
uint64_t sys_setfsuid(uint64_t *regs) { RET(get_current_process()->cred.euid); }
uint64_t sys_setfsgid(uint64_t *regs) { RET(get_current_process()->cred.egid); }
uint64_t sys_getgroups(uint64_t *regs)
{
    process_t *p = get_current_process();
    int64_t size = (int64_t)regs[9];
    int n = p->cred.ngroups;
    if (size == 0)
        RET(n);
    if (size < n)
        RET(-EINVAL);
    if (n && copy_to_user(p, regs[10], p->cred.groups, (uint64_t)n * 4) < 0)
        RET(-EFAULT);
    RET(n);
}
uint64_t sys_setgroups(uint64_t *regs)
{
    process_t *p = get_current_process();
    int64_t n = (int64_t)regs[9];
    if (p->cred.euid != 0)
        RET(-EPERM);
    if (n < 0 || n > NGROUPS_MAX_)
        RET(-EINVAL);
    uint32_t g[NGROUPS_MAX_];
    if (n && copy_from_user(p, g, regs[10], (uint64_t)n * 4) < 0)
        RET(-EFAULT);
    for (int i = 0; i < n; i++)
        p->cred.groups[i] = g[i];
    p->cred.ngroups = (int)n;
    RET(0);
}
uint64_t sys_getresuid(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint32_t v[3] = { p->cred.uid, p->cred.euid, p->cred.suid };
    for (int i = 0; i < 3; i++)
        if (regs[9 + i] && copy_to_user(p, regs[9 + i], &v[i], 4) < 0)
            RET(-EFAULT);
    RET(0);
}
uint64_t sys_getresgid(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint32_t v[3] = { p->cred.gid, p->cred.egid, p->cred.sgid };
    for (int i = 0; i < 3; i++)
        if (regs[9 + i] && copy_to_user(p, regs[9 + i], &v[i], 4) < 0)
            RET(-EFAULT);
    RET(0);
}
uint64_t sys_umask_(uint64_t *regs)
{
    cred_t *c = &get_current_process()->cred;
    uint32_t old = c->umask;
    c->umask = (uint32_t)regs[9] & 0777;
    RET(old);
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
