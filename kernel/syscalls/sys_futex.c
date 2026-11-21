#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "kfile.h"
#include "ktime.h"
#include "vmm.h"
#include "numbers.h"
#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_CLOCK_REALTIME 256
static char futex_chan;
struct kts { int64_t sec, nsec; };
#define NSEC 1000000000LL
int futex_wake_phys(uint64_t phys, int n)
{
    int woken = 0;
    for (int i = 0; i < MAX_PROCESSES && woken < n; i++)
    {
        process_t *t = &processes[i];
        if (t->state == PROCESS_STATE_WAITING && t->wchan == &futex_chan && t->futex_key == phys)
        {
            t->futex_woken = 1;
            t->wchan = 0;
            t->wake_tick = 0;
            t->state = PROCESS_STATE_READY;
            woken++;
        }
    }
    return woken;
}
static int futex_requeue_phys(uint64_t from, uint64_t to, int n)
{
    int moved = 0;
    for (int i = 0; i < MAX_PROCESSES && moved < n; i++)
    {
        process_t *t = &processes[i];
        if (t->state == PROCESS_STATE_WAITING && t->wchan == &futex_chan && t->futex_key == from)
        {
            t->futex_key = to;
            moved++;
        }
    }
    return moved;
}
static uint64_t futex_wait(uint64_t *regs, uint64_t uaddr, uint32_t val, uint64_t tsp, int absolute, int realtime)
{
    process_t *me = get_current_process();
    uint64_t ph = vmm_virt_to_phys(me->pml4, uaddr);
    if (!ph)
        RET(-EFAULT);
    if (me->futex_key)
    {
        if (me->futex_woken)
        {
            me->futex_woken = 0;
            me->futex_key = 0;
            me->tmo_active = 0;
            RET(0);
        }
        if (tsp && me->tmo_active && ktime_ticks >= me->tmo_deadline)
        {
            me->futex_key = 0;
            me->tmo_active = 0;
            RET(-ETIMEDOUT);
        }
        goto sleep;
    }
    if (*(volatile uint32_t *)ph != val)
        RET(-EAGAIN);
    me->futex_key = ph;
    me->futex_woken = 0;
    me->tmo_active = 0;
    if (tsp)
    {
        struct kts t;
        if (copy_from_user(me, &t, tsp, sizeof t) < 0)
        {
            me->futex_key = 0;
            RET(-EFAULT);
        }
        if (t.sec < 0 || t.nsec < 0 || t.nsec >= NSEC)
        {
            me->futex_key = 0;
            RET(-EINVAL);
        }
        int64_t ns = t.sec * NSEC + t.nsec;
        if (absolute)
            ns -= (int64_t)(realtime ? ktime_realtime_ns() : ktime_monotonic_ns());
        if (ns <= 0)
        {
            me->futex_key = 0;
            RET(-ETIMEDOUT);
        }
        uint64_t ticks = ((uint64_t)ns + (NSEC / HZ) - 1) / (NSEC / HZ);
        me->tmo_active = 1;
        me->tmo_deadline = ktime_ticks + ticks;
    }
sleep:
    return sleep_until(regs, NR_futex, &futex_chan, tsp ? me->tmo_deadline : 0);
}
uint64_t sys_futex(uint64_t *regs)
{
    process_t *me = get_current_process();
    uint64_t uaddr = regs[9], uaddr2 = regs[7];
    int op = (int)regs[10] & 0x7F;
    int realtime = ((int)regs[10] & FUTEX_CLOCK_REALTIME) != 0;
    uint32_t val = (uint32_t)regs[11], val3 = (uint32_t)regs[6];
    uint64_t arg4 = regs[5];
    if (uaddr & 3)
        RET(-EINVAL);
    switch (op)
    {
    case FUTEX_WAIT:
        return futex_wait(regs, uaddr, val, arg4, 0, 0);
    case FUTEX_WAIT_BITSET:
        if (val3 == 0)
            RET(-EINVAL);
        return futex_wait(regs, uaddr, val, arg4, 1, realtime);
    case FUTEX_WAKE:
    case FUTEX_WAKE_BITSET:
    {
        uint64_t ph = vmm_virt_to_phys(me->pml4, uaddr);
        if (!ph)
            RET(-EFAULT);
        RET(futex_wake_phys(ph, (int)val < 0 ? 0x7FFFFFFF : (int)val));
    }
    case FUTEX_REQUEUE:
    case FUTEX_CMP_REQUEUE:
    {
        if (uaddr2 & 3)
            RET(-EINVAL);
        uint64_t ph = vmm_virt_to_phys(me->pml4, uaddr);
        uint64_t ph2 = vmm_virt_to_phys(me->pml4, uaddr2);
        if (!ph || !ph2)
            RET(-EFAULT);
        if (op == FUTEX_CMP_REQUEUE && *(volatile uint32_t *)ph != val3)
            RET(-EAGAIN);
        int w = futex_wake_phys(ph, (int)val < 0 ? 0x7FFFFFFF : (int)val);
        int m = futex_requeue_phys(ph, ph2, (int)arg4 < 0 ? 0x7FFFFFFF : (int)arg4);
        RET(w + m);
    }
    }
    RET(-ENOSYS);
}
