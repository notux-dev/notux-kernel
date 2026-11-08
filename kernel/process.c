#include "process.h"
#include "kfile.h"
#include "stdint.h"
#include "vmm.h"
#include "serial.h"
#include "pmm.h"
process_t processes[MAX_PROCESSES];
int current_process_idx = 0;
static uint64_t next_pid = 1;
static uint8_t *kstack_mem[MAX_PROCESSES];
static uint64_t kstack_top(int idx)
{
    if (!kstack_mem[idx])
        kstack_mem[idx] = pmm_alloc_contig(KERNEL_STACK_SIZE / 4096);
    return kstack_mem[idx] ? (uint64_t)kstack_mem[idx] + KERNEL_STACK_SIZE : 0;
}
extern void set_kernel_stack(uint64_t stack);

static uint8_t *fpu_mem;
static uint8_t *fpu_area(int idx)
{
    if (!fpu_mem)
        fpu_mem = pmm_alloc_contig((MAX_PROCESSES * 512 + 4095) / 4096);
    return fpu_mem ? fpu_mem + idx * 512 : 0;
}
void process_fpu_init(int idx)
{
    uint8_t *a = fpu_area(idx);
    if (!a)
        return;
    for (int i = 0; i < 512; i++)
        a[i] = 0;
    *(uint16_t *)(a + 0) = 0x037F;
    *(uint32_t *)(a + 24) = 0x1F80;
}
void process_fpu_save_to(int idx)
{
    uint8_t *a = fpu_area(idx);
    if (a)
        asm volatile("fxsave (%0)" : : "r"(a) : "memory");
}
void process_fpu_reset_current(void)
{
    process_fpu_init(current_process_idx);
    uint8_t *a = fpu_area(current_process_idx);
    if (a)
        asm volatile("fxrstor (%0)" : : "r"(a) : "memory");
}
static void fpu_restore(int idx)
{
    uint8_t *a = fpu_area(idx);
    if (a)
        asm volatile("fxrstor (%0)" : : "r"(a) : "memory");
}
void scheduler_init(void)
{
    for (int i = 0;
         i < MAX_PROCESSES;
         i++)
    {
        processes[i].pid = 0;
        processes[i].ppid = 0;
        processes[i].exit_code = 0;
        processes[i].esp = 0;
        processes[i].kernel_stack = 0;
        processes[i].state =
            PROCESS_STATE_TERMINATED;
        processes[i].pml4 = 0;
        processes[i].heap_start = 0;
        processes[i].heap_end = 0;
        processes[i].nvmas = 0;
        processes[i].fs_base = 0;
        processes[i].cwd = 0;
        for (int j = 0; j < MAX_FDS; j++)
        {
            processes[i].fds[j] = 0;
            processes[i].fd_flags[j] = 0;
        }
    }
    current_process_idx = 0;
    next_pid = 1;
    serwrite(
        "SCHEDULER: initialized\n"
    );
}
process_t *get_current_process(void)
{
    return &processes[
        current_process_idx
    ];
}
process_t *get_process_by_pid(
    uint32_t pid
)
{
    for (int i = 0;
         i < MAX_PROCESSES;
         i++)
    {
        if (processes[i].pid == pid &&
            processes[i].state !=
                PROCESS_STATE_TERMINATED)
        {
            return &processes[i];
        }
    }
    return 0;
}
int create_process(
    uint64_t entry_point
)
{
    int idx = -1;
    for (int i = 0;
         i < MAX_PROCESSES;
         i++)
    {
        if (processes[i].state ==
            PROCESS_STATE_TERMINATED)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        serwrite(
            "PROCESS: no free slot\n"
        );
        return -1;
    }
    uint64_t *pml4 =
        create_user_pml4();
    if (!pml4)
    {
        serwrite(
            "PROCESS: create_user_pml4 failed\n"
        );
        return -1;
    }
    processes[idx].pml4 =
        pml4;
    uint64_t kernel_stack_top = kstack_top(idx);
    uint64_t user_stack_top = USER_STACK_TOP;
    for (uint64_t off = 0; off < USER_STACK_SIZE; off += PAGE_SIZE)
    {
        uint8_t *sp = (uint8_t *)pmm_alloc_page();
        if (!sp)
        {
            serwrite("PROCESS: no memory for user stack\n");
            return -1;
        }
        for (int b = 0; b < PAGE_SIZE; b++)
            sp[b] = 0;
        vmm_map_page(
            pml4,
            USER_STACK_TOP - USER_STACK_SIZE + off,
            (uint64_t)sp,
            PAGE_USER | PAGE_WRITE | PAGE_OWNED);
    }
    processes[idx].kernel_stack =
        kernel_stack_top;
    uint64_t *stack =
        (uint64_t *)kernel_stack_top;
    *(--stack) =
        0x1B;
    *(--stack) =
        user_stack_top;
    *(--stack) =
        0x202;
    *(--stack) =
        0x23;
    *(--stack) =
        entry_point;
    for (int i = 0;
         i < 15;
         i++)
    {
        *(--stack) = 0;
    }
    processes[idx].esp =
        (uint64_t)stack;
    processes[idx].pid =
        (uint32_t)next_pid++;
    processes[idx].pgid = processes[idx].pid;
    processes[idx].sid = processes[idx].pid;
    process_t *caller =
        get_current_process();
    processes[idx].ppid =
        (caller &&
         caller->state == PROCESS_STATE_RUNNING)
            ? caller->pid
            : 0;
    processes[idx].exit_code = 0;
    processes[idx].heap_start = 0;
    processes[idx].heap_end = 0;
    processes[idx].nvmas = 0;
    processes[idx].fs_base = 0;
    processes[idx].cwd = 0;
    processes[idx].wchan = 0;
    processes[idx].sigmask = 0;
    processes[idx].sigpending = 0;
    processes[idx].restart_frame = 0;
    processes[idx].sigsusp = 0;
    processes[idx].alarm_tick = 0;
    processes[idx].alarm_interval = 0;
    process_fpu_init(idx);
    processes[idx].sigpending = 0;
    processes[idx].exit_signal = 0;
    for (int s_ = 0; s_ < NSIG_; s_++)
        processes[idx].sigact[s_] = (ksigaction_t){0, 0, 0, 0};
    process_init_limits(&processes[idx]);
    processes[idx].wake_tick = 0;
    processes[idx].tmo_active = 0;
    fdt_init(&processes[idx]);
    processes[idx].state =
        PROCESS_STATE_READY;
    serwrite(
        "PROCESS: created\n"
    );
    return processes[idx].pid;
}
void set_process_entry(
    process_t *proc,
    uint64_t entry_point
)
{
    if (!proc)
        return;
    if (!proc->esp)
        return;
    uint64_t *stack =
        (uint64_t *)proc->esp;
    stack[15] =
        entry_point;
}
void set_process_argv(
    process_t *proc,
    uint64_t argc,
    uint64_t argv_ptr
)
{
    if (!proc)
        return;
    if (!proc->esp)
        return;
    uint64_t *stack =
        (uint64_t *)proc->esp;
    stack[9] = argc;
    stack[10] = argv_ptr;
}
static void activate_process(
    int idx
)
{
    if (idx < 0)
        return;
    if (idx >= MAX_PROCESSES)
        return;
    current_process_idx =
        idx;
    processes[idx].state =
        PROCESS_STATE_RUNNING;
    set_kernel_stack(
        processes[idx].kernel_stack
    );
    if (processes[idx].pml4)
    {
        vmm_switch_pml4(
            processes[idx].pml4
        );
    }
    {
        uint64_t fb = processes[idx].fs_base;
        asm volatile("wrmsr" : : "c"(0xC0000100u),
                     "a"((uint32_t)fb), "d"((uint32_t)(fb >> 32)));
    }
}
static int idle_active = 0;
uint64_t schedule(
    uint64_t current_esp
)
{
    process_t *current =
        &processes[
            current_process_idx
        ];
    if (current->state ==
        PROCESS_STATE_RUNNING)
    {
        current->esp =
            current_esp;
    }
    int next_idx = -1;
    int prev_idx = current_process_idx;
    for (int i = 1;
         i < MAX_PROCESSES;
         i++)
    {
        int idx =
            (current_process_idx + i)
            % MAX_PROCESSES;
        if (processes[idx].state ==
            PROCESS_STATE_READY)
        {
            next_idx =
                idx;
            break;
        }
    }
    if (next_idx >= 0)
    {
        idle_active = 0;
        if (current->state ==
            PROCESS_STATE_RUNNING)
        {
            current->state =
                PROCESS_STATE_READY;
        }
        process_fpu_save_to(prev_idx);
        activate_process(
            next_idx
        );
        fpu_restore(next_idx);
        return processes[
            current_process_idx
        ].esp;
    }
    if (current->state !=
        PROCESS_STATE_TERMINATED &&
        current->state !=
        PROCESS_STATE_ZOMBIE &&
        current->state !=
        PROCESS_STATE_WAITING)
    {
        idle_active = 0;
        current->state =
            PROCESS_STATE_RUNNING;
        return current->esp;
    }
    if (idle_active)
        return current_esp;
    idle_active = 1;
    asm volatile("sti");
    for (;;)
    {
        asm volatile("hlt");
    }
}
void wakeup(void *chan)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].state == PROCESS_STATE_WAITING &&
            processes[i].wchan == chan)
        {
            processes[i].wchan = 0;
            processes[i].wake_tick = 0;
            processes[i].state = PROCESS_STATE_READY;
        }
}

uint64_t sleep_on(uint64_t *regs, uint64_t nr, void *chan)
{
    return sleep_until(regs, nr, chan, 0);
}

uint64_t sleep_until(uint64_t *regs, uint64_t nr, void *chan, uint64_t wake_tick)
{
    process_t *me = &processes[current_process_idx];
    me->wake_tick = wake_tick;
    regs[15] -= 2;
    regs[14] = nr;
    me->restart_frame = 1;
    me->wchan = chan;
    me->esp = (uint64_t)regs;
    me->state = PROCESS_STATE_WAITING;
    return schedule((uint64_t)regs);
}

static void make_zombie(process_t *me)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_t *c = &processes[i];
        if (c->state == PROCESS_STATE_TERMINATED || c == me ||
            c->ppid != me->pid || me->pid == 1)
            continue;
        c->ppid = 1;
        if (c->state == PROCESS_STATE_ZOMBIE)
        {
            process_t *init = get_process_by_pid(1);
            if (init)
                wakeup(init);
        }
    }
    fdt_close_all(me);
    me->state = PROCESS_STATE_ZOMBIE;
    process_t *parent = get_process_by_pid(me->ppid);
    if (parent)
        wakeup(parent);
    me->sigpending = 0;
    if (parent)
        signal_post(parent, 17);
}

static void do_exit(int exit_code, int sig)
{
    process_t *me = &processes[current_process_idx];
    me->exit_code = exit_code;
    me->exit_signal = sig;
    make_zombie(me);
    serwrite(
        "PROCESS: zombie\n"
    );
    asm volatile("sti");
    for (;;)
    {
        asm volatile("hlt");
    }
}
extern void iput(void *ip);

void exit_current_process(int exit_code)
{
    do_exit(exit_code, 0);
}

void exit_by_signal(int sig)
{
    do_exit(0, sig);
}

void process_signal_death(process_t *t, int sig)
{
    t->exit_code = 0;
    t->exit_signal = sig;
    make_zombie(t);
}

void process_init_limits(process_t *p)
{
    const uint64_t INF = ~0ULL;
    for (int i = 0; i < 16; i++)
        p->rlim[i][0] = p->rlim[i][1] = INF;
    p->rlim[3][0] = 8 * 1024 * 1024;
    p->rlim[3][1] = INF;
    p->rlim[4][0] = p->rlim[4][1] = 0;
    p->rlim[7][0] = p->rlim[7][1] = MAX_FDS;
}

void process_reap(process_t *p)
{
    if (p->cwd)
    {
        iput((void *)p->cwd);
        p->cwd = 0;
    }
    fdt_close_all(p);
    uint64_t *pml4 = p->pml4;
    p->pml4 = 0;
    p->state = PROCESS_STATE_TERMINATED;
    p->pid = 0;
    p->ppid = 0;
    vmm_free_user_space(pml4);
}
int process_fork(uint64_t *regs, process_t **child_out)
{
    process_t *parent = get_current_process();
    int idx = -1;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].state == PROCESS_STATE_TERMINATED)
        {
            idx = i;
            break;
        }
    if (idx < 0)
        return -1;
    uint64_t *pml4 = create_user_pml4();
    if (!pml4)
        return -1;
    if (vmm_copy_user_space(parent->pml4, pml4) < 0)
    {
        vmm_free_user_space(pml4);
        return -1;
    }
    process_t *c = &processes[idx];
    c->pml4 = pml4;
    c->kernel_stack = kstack_top(idx);
    uint64_t *frame = (uint64_t *)c->kernel_stack - 20;
    for (int i = 0; i < 20; i++)
        frame[i] = regs[i];
    frame[14] = 0;
    c->esp = (uint64_t)frame;
    c->pid = (uint32_t)next_pid++;
    c->ppid = parent->pid;
    c->pgid = parent->pgid;
    c->sid = parent->sid;
    c->exit_code = 0;
    c->heap_start = parent->heap_start;
    c->heap_end = parent->heap_end;
    c->nvmas = parent->nvmas;
    c->fs_base = parent->fs_base;
    c->wchan = 0;
    c->sigmask = parent->sigmask;
    c->sigpending = 0;
    c->restart_frame = 0;
    c->sigsusp = 0;
    c->saved_mask = 0;
    c->alarm_tick = 0;
    c->alarm_interval = 0;
    c->exit_signal = 0;
    for (int i_ = 0; i_ < 16; i_++)
        c->comm[i_] = parent->comm[i_];
    process_fpu_save_to(idx);
    for (int s_ = 0; s_ < NSIG_; s_++)
        c->sigact[s_] = parent->sigact[s_];
    for (int r_ = 0; r_ < 16; r_++)
    {
        c->rlim[r_][0] = parent->rlim[r_][0];
        c->rlim[r_][1] = parent->rlim[r_][1];
    }
    c->wake_tick = 0;
    c->tmo_active = 0;
    c->cwd = parent->cwd;
    for (int i = 0; i < parent->nvmas; i++)
        c->vmas[i] = parent->vmas[i];
    for (int i = 0; i < MAX_FDS; i++)
    {
        c->fds[i] = 0;
        c->fd_flags[i] = 0;
    }
    c->state = PROCESS_STATE_READY;
    *child_out = c;
    return (int)c->pid;
}
