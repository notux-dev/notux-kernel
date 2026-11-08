#include "syscalls.h"
#include "process.h"

uint64_t sys_wait4(uint64_t *regs)
{
    int64_t pid = (int64_t)regs[9];
    int *status = (int *)regs[10];
    uint64_t options = regs[11];
    process_t *me = get_current_process();

    int have_child = 0;
    process_t *zombie = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_t *c = &processes[i];
        if (c->state == PROCESS_STATE_TERMINATED || c->ppid != me->pid ||
            c == me)
            continue;
        if (pid > 0 && c->pid != (uint32_t)pid)
            continue;
        have_child = 1;
        if (c->state == PROCESS_STATE_ZOMBIE && !zombie)
            zombie = c;
    }
    if (!have_child)
    {
        regs[14] = (uint64_t)-ECHILD;
        return (uint64_t)regs;
    }
    if (zombie)
    {
        uint32_t cpid = zombie->pid;
        if (status)
            *status = zombie->exit_signal ? (zombie->exit_signal & 0x7F)
                                          : (zombie->exit_code & 0xFF) << 8;
        process_reap(zombie);
        regs[14] = cpid;
        return (uint64_t)regs;
    }
    if (options & WNOHANG)
    {
        regs[14] = 0;
        return (uint64_t)regs;
    }
    return sleep_on(regs, NR_wait4, me);
}
