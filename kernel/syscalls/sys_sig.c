#include "syscalls.h"
#include "process.h"
#include "uaccess.h"


#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)

#define SIGKILL 9
#define SIGSTOP 19
#define SIGCHLD 17
#define SIGCONT 18
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG  23
#define SIGWINCH 28

#define SIG_BIT(s) (1ULL << ((s) - 1))
#define UNBLOCKABLE (SIG_BIT(SIGKILL) | SIG_BIT(SIGSTOP))

static int default_ignores(int sig)
{
    return sig == SIGCHLD || sig == SIGCONT || sig == SIGURG || sig == SIGWINCH ||
           sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU;
}

uint64_t sys_rt_sigaction(uint64_t *regs)
{
    process_t *p = get_current_process();
    int sig = (int)regs[9];
    uint64_t act = regs[10], oact = regs[11], size = regs[5];
    if (size != 8 || sig < 1 || sig > NSIG_)
        RET(-EINVAL);
    if (act && (sig == SIGKILL || sig == SIGSTOP))
        RET(-EINVAL);
    ksigaction_t old = p->sigact[sig - 1];
    if (act)
    {
        ksigaction_t n;
        if (copy_from_user(p, &n, act, sizeof n) < 0)
            RET(-EFAULT);
        n.mask &= ~UNBLOCKABLE;
        if (oact && copy_to_user(p, oact, &old, sizeof old) < 0)
            RET(-EFAULT);
        p->sigact[sig - 1] = n;
        if (n.handler == 1)
            p->sigpending &= ~SIG_BIT(sig);
        RET(0);
    }
    if (oact && copy_to_user(p, oact, &old, sizeof old) < 0)
        RET(-EFAULT);
    RET(0);
}

uint64_t sys_rt_sigprocmask(uint64_t *regs)
{
    process_t *p = get_current_process();
    int how = (int)regs[9];
    uint64_t set = regs[10], oset = regs[11], size = regs[5];
    if (size != 8)
        RET(-EINVAL);
    uint64_t old = p->sigmask;
    if (set)
    {
        uint64_t n;
        if (copy_from_user(p, &n, set, sizeof n) < 0)
            RET(-EFAULT);
        n &= ~UNBLOCKABLE;
        if (how == 0)
            p->sigmask |= n;
        else if (how == 1)
            p->sigmask &= ~n;
        else if (how == 2)
            p->sigmask = n;
        else
            RET(-EINVAL);
    }
    if (oset && copy_to_user(p, oset, &old, sizeof old) < 0)
        RET(-EFAULT);
    RET(0);
}

uint64_t sys_rt_sigpending(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t v = p->sigpending & p->sigmask;
    if (regs[10] != 8)
        RET(-EINVAL);
    if (copy_to_user(p, regs[9], &v, sizeof v) < 0)
        RET(-EFAULT);
    RET(0);
}

uint64_t sys_rt_sigsuspend(uint64_t *regs)
{
    process_t *p = get_current_process();
    if (!p->sigsusp)
    {
        uint64_t m;
        if (regs[10] != 8)
            RET(-EINVAL);
        if (copy_from_user(p, &m, regs[9], sizeof m) < 0)
            RET(-EFAULT);
        p->saved_mask = p->sigmask;
        p->sigmask = m & ~UNBLOCKABLE;
        p->sigsusp = 1;
    }
    if (p->sigpending & ~p->sigmask)
        RET(-EINTR);
    return sleep_on(regs, 130, p);
}

uint64_t sys_pause(uint64_t *regs)
{
    process_t *p = get_current_process();
    if (p->sigpending & ~p->sigmask)
        RET(-EINTR);
    return sleep_on(regs, 34, p);
}

uint64_t sys_sigaltstack(uint64_t *regs)
{
    process_t *p = get_current_process();
    if (regs[10])
    {
        struct { uint64_t sp; int32_t flags; int32_t pad; uint64_t size; } o = {0, 2  , 0, 0};
        if (copy_to_user(p, regs[10], &o, sizeof o) < 0)
            RET(-EFAULT);
    }
    RET(0);
}


void signal_post(process_t *t, int sig)
{
    if (sig < 1 || sig > NSIG_ || t->state == PROCESS_STATE_ZOMBIE ||
        t->state == PROCESS_STATE_TERMINATED)
        return;
    if (sig == SIGKILL)
    {
        if (t == get_current_process())
            exit_by_signal(sig);
        process_signal_death(t, sig);
        return;
    }
    ksigaction_t *a = &t->sigact[sig - 1];
    if (a->handler == 1)
        return;
    if (a->handler == 0 && default_ignores(sig))
        return;
    t->sigpending |= SIG_BIT(sig);
    if (t->state == PROCESS_STATE_WAITING && !(t->sigmask & SIG_BIT(sig)))
    {
        t->wchan = 0;
        t->wake_tick = 0;
        t->state = PROCESS_STATE_READY;
    }
}

static int send_signal(process_t *t, int sig)
{
    if (sig == 0)
        return 0;
    if (sig < 1 || sig > NSIG_)
        return -EINVAL;
    signal_post(t, sig);
    return 0;
}


#define SA_RESTART   0x10000000ULL
#define SA_NODEFER   0x40000000ULL
#define SA_RESETHAND 0x80000000ULL
#define SA_RESTORER  0x04000000ULL

#define UC_SIZE 320
#define SI_SIZE 128

static uint8_t fxbuf[512] __attribute__((aligned(16)));

static int never_restart(uint64_t nr)
{
    return nr == 7 || nr == 23 || nr == 34 || nr == 35 || nr == 130 ||
           nr == 230 || nr == 270 || nr == 271;
}

static void build_frame(process_t *p, uint64_t *f, int sig, ksigaction_t *a, uint64_t oldmask)
{
    uint64_t sp = f[18] - 128;
    uint64_t fp = (sp - 512) & ~63ULL;
    uint64_t uc = fp - UC_SIZE;
    uint64_t info = uc - SI_SIZE;
    uint64_t entry_sp = info - 8;

    uint64_t u[UC_SIZE / 8];
    for (int i = 0; i < UC_SIZE / 8; i++)
        u[i] = 0;
    uint64_t *g = &u[5];
    g[0] = f[7];  g[1] = f[6];  g[2] = f[5];  g[3] = f[4];
    g[4] = f[3];  g[5] = f[2];  g[6] = f[1];  g[7] = f[0];
    g[8] = f[9];  g[9] = f[10]; g[10] = f[8]; g[11] = f[13];
    g[12] = f[11]; g[13] = f[14]; g[14] = f[12]; g[15] = f[18];
    g[16] = f[15]; g[17] = f[17];
    g[18] = 0x23;
    g[21] = oldmask;
    g[23] = fp;
    u[5 + 32] = oldmask;

    uint8_t si[SI_SIZE];
    for (int i = 0; i < SI_SIZE; i++)
        si[i] = 0;
    *(int32_t *)si = sig;
    if (sig == 17)
        *(int32_t *)(si + 8) = 1;

    asm volatile("fxsave (%0)" : : "r"(fxbuf) : "memory");
    uint64_t ret = a->restorer;

    if (!(a->flags & SA_RESTORER) || !ret ||
        copy_to_user(p, fp, fxbuf, 512) < 0 ||
        copy_to_user(p, uc, u, UC_SIZE) < 0 ||
        copy_to_user(p, info, si, SI_SIZE) < 0 ||
        copy_to_user(p, entry_sp, &ret, 8) < 0)
        exit_by_signal(11);

    f[15] = a->handler;
    f[18] = entry_sp;
    f[9] = (uint64_t)sig;
    f[10] = info;
    f[11] = uc;
    f[14] = 0;
    f[17] &= ~((1ULL << 10) | (1ULL << 8));
    uint64_t add = a->mask | (a->flags & SA_NODEFER ? 0 : SIG_BIT(sig));
    p->sigmask = (oldmask | add) & ~UNBLOCKABLE;
    if (a->flags & SA_RESETHAND)
        a->handler = 0;
    {
        uint8_t clean[512] __attribute__((aligned(16)));
        for (int i = 0; i < 512; i++)
            clean[i] = 0;
        *(uint16_t *)(clean + 0) = 0x037F;
        *(uint32_t *)(clean + 24) = 0x1F80;
        asm volatile("fxrstor (%0)" : : "r"(clean) : "memory");
    }
}

uint64_t signal_check(uint64_t *f)
{
    if ((f[16] & 3) != 3)
        return (uint64_t)f;
    process_t *p = get_current_process();
    int restart = p->restart_frame;
    p->restart_frame = 0;
    if (restart && f[14] == 61)
    {
        int64_t want = (int64_t)f[9];
        for (int i = 0; i < MAX_PROCESSES; i++)
            if (processes[i].state == PROCESS_STATE_ZOMBIE && processes[i].ppid == p->pid &&
                (want <= 0 || processes[i].pid == (uint32_t)want))
                return (uint64_t)f;
    }
    for (;;)
    {
        uint64_t ready = p->sigpending & ~p->sigmask;
        if (!ready)
            break;
        int sig = __builtin_ctzll(ready) + 1;
        p->sigpending &= ~SIG_BIT(sig);
        ksigaction_t *a = &p->sigact[sig - 1];
        if (a->handler == 1)
            continue;
        if (a->handler == 0)
        {
            if (default_ignores(sig))
                continue;
            exit_by_signal(sig);
        }
        uint64_t oldmask = p->sigmask;
        if (p->sigsusp)
        {
            oldmask = p->saved_mask;
            p->sigsusp = 0;
        }
        if (restart)
        {
            uint64_t nr = f[14];
            if (never_restart(nr) || !(a->flags & SA_RESTART))
            {
                f[14] = (uint64_t)(int64_t)-EINTR;
                f[15] += 2;
            }
            p->tmo_active = 0;
        }
        build_frame(p, f, sig, a, oldmask);
        return (uint64_t)f;
    }
    if (p->sigsusp && !restart)
    {
        p->sigmask = p->saved_mask;
        p->sigsusp = 0;
    }
    return (uint64_t)f;
}

uint64_t sys_rt_sigreturn(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t uc = regs[18] + SI_SIZE;
    uint64_t u[UC_SIZE / 8];
    if (copy_from_user(p, u, uc, 40 + 256 + 8) < 0)
        exit_by_signal(11);
    uint64_t *g = &u[5];
    regs[7] = g[0];  regs[6] = g[1];  regs[5] = g[2];  regs[4] = g[3];
    regs[3] = g[4];  regs[2] = g[5];  regs[1] = g[6];  regs[0] = g[7];
    regs[9] = g[8];  regs[10] = g[9]; regs[8] = g[10]; regs[13] = g[11];
    regs[11] = g[12]; regs[14] = g[13]; regs[12] = g[14]; regs[18] = g[15];
    regs[15] = g[16];
    regs[17] = (g[17] & 0xCD5) | 0x202;
    regs[16] = 0x23;
    regs[19] = 0x1B;
    p->sigmask = u[5 + 32] & ~UNBLOCKABLE;
    if (g[23] && copy_from_user(p, fxbuf, g[23], 512) == 0)
        asm volatile("fxrstor (%0)" : : "r"(fxbuf) : "memory");
    return (uint64_t)regs;
}

static int do_kill(int64_t pid, int sig)
{
    process_t *me = get_current_process();
    int found = 0, r = 0;
    if (sig < 0 || sig > NSIG_)
        return -EINVAL;
    if (pid > 0)
    {
        process_t *t = get_process_by_pid((uint32_t)pid);
        if (!t || t->state == PROCESS_STATE_TERMINATED)
            return -ESRCH;
        return send_signal(t, sig);
    }
    if (pid == 0 || pid < -1)
    {
        uint32_t pg = pid == 0 ? me->pgid : (uint32_t)(-pid);
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            process_t *t = &processes[i];
            if (t->state == PROCESS_STATE_TERMINATED || t == me || t->pgid != pg)
                continue;
            found = 1;
            r = send_signal(t, sig);
        }
        if (me->pgid == pg)
        {
            found = 1;
            r = send_signal(me, sig);
        }
        return found ? r : -ESRCH;
    }
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_t *t = &processes[i];
        if (t->state == PROCESS_STATE_TERMINATED || t == me || t->pid == 1)
            continue;
        found = 1;
        r = send_signal(t, sig);
    }
    return found ? r : -ESRCH;
}

uint64_t sys_kill(uint64_t *regs)
{
    RET(do_kill((int64_t)regs[9], (int)regs[10]));
}

uint64_t sys_tkill(uint64_t *regs)
{
    if ((int64_t)regs[9] <= 0)
        RET(-EINVAL);
    RET(do_kill((int64_t)regs[9], (int)regs[10]));
}

uint64_t sys_tgkill(uint64_t *regs)
{
    if ((int64_t)regs[10] <= 0)
        RET(-EINVAL);
    RET(do_kill((int64_t)regs[10], (int)regs[11]));
}
