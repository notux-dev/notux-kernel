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
extern void iput(void *ip);
static pshared_t shared_pool[MAX_SHARED];
static pshared_t *sh_alloc(void)
{
    for (int i = 0; i < MAX_SHARED; i++)
        if (shared_pool[i].refs == 0)
        {
            uint8_t *b = (uint8_t *)&shared_pool[i];
            for (uint64_t k = 0; k < sizeof(pshared_t); k++)
                b[k] = 0;
            shared_pool[i].refs = 1;
            return &shared_pool[i];
        }
    return 0;
}
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
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        processes[i].pid = 0;
        processes[i].tgid = 0;
        processes[i].ppid = 0;
        processes[i].exit_code = 0;
        processes[i].esp = 0;
        processes[i].kernel_stack = 0;
        processes[i].state = PROCESS_STATE_TERMINATED;
        processes[i].pml4 = 0;
        processes[i].sh = 0;
        processes[i].fs_base = 0;
    }
    for (int i = 0; i < MAX_SHARED; i++)
        shared_pool[i].refs = 0;
    current_process_idx = 0;
    next_pid = 1;
    serwrite("SCHEDULER: initialized\n");
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
    processes[idx].sh = sh_alloc();
    if (!processes[idx].sh)
    {
        vmm_free_user_space(pml4);
        serwrite("PROCESS: no shared-state slot\n");
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
        page_zero(sp);
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
    processes[idx].tgid = processes[idx].pid;
    processes[idx].is_thread = 0;
    processes[idx].leader_exited = 0;
    processes[idx].clear_tid = 0;
    processes[idx].futex_key = 0;
    processes[idx].futex_woken = 0;
    processes[idx].pgid = processes[idx].pid;
    processes[idx].sid = processes[idx].pid;
    process_t *caller =
        get_current_process();
    processes[idx].ppid =
        (caller &&
         caller->state == PROCESS_STATE_RUNNING)
            ? caller->pid
            : 0;
    if (caller && caller->state == PROCESS_STATE_RUNNING)
        processes[idx].cred = caller->cred;
    else
    {
        for (unsigned k_ = 0; k_ < sizeof(cred_t); k_++)
            ((char *)&processes[idx].cred)[k_] = 0;
        processes[idx].cred.umask = 022;
    }
    processes[idx].exit_code = 0;
    processes[idx].sh->heap_start = 0;
    processes[idx].sh->heap_end = 0;
    processes[idx].sh->nvmas = 0;
    processes[idx].fs_base = 0;
    processes[idx].sh->cwd = 0;
    processes[idx].sh->root = 0;
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
        processes[idx].sh->sigact[s_] = (ksigaction_t){0, 0, 0, 0};
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
            c->ppid != me->tgid || me->tgid == 1)
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
static void halt_forever(void)
{
    asm volatile("sti");
    for (;;)
    {
        asm volatile("hlt");
    }
}
process_t *process_leader(process_t *p)
{
    if (!p->is_thread)
        return p;
    process_t *l = get_process_by_pid(p->tgid);
    return l ? l : p;
}
int process_nthreads(process_t *p)
{
    int n = 0;
    if (!p->sh)
        return 1;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].state != PROCESS_STATE_TERMINATED && processes[i].sh == p->sh)
            n++;
    return n;
}
static void kill_threads_except(process_t *p, process_t *a, process_t *b)
{
    pshared_t *sh = p->sh;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_t *t = &processes[i];
        if (t == a || t == b || t->state == PROCESS_STATE_TERMINATED || t->sh != sh)
            continue;
        t->state = PROCESS_STATE_TERMINATED;
        t->pid = 0;
        t->wchan = 0;
        sh->refs--;
    }
}
void process_kill_other_threads(process_t *me)
{
    kill_threads_except(me, me, 0);
}
void group_exit(int exit_code, int sig)
{
    process_t *me = &processes[current_process_idx];
    process_t *l = process_leader(me);
    kill_threads_except(me, me, l);
    l->exit_code = exit_code;
    l->exit_signal = sig;
    l->leader_exited = 0;
    make_zombie(l);
    if (me != l)
    {
        me->state = PROCESS_STATE_TERMINATED;
        me->pid = 0;
        me->sh->refs--;
    }
    serwrite("PROCESS: zombie\n");
    halt_forever();
}
extern int futex_wake_phys(uint64_t phys, int n);
void thread_exit(int exit_code)
{
    process_t *me = &processes[current_process_idx];
    process_t *l = process_leader(me);
    if (process_nthreads(me) <= 1)
        group_exit(exit_code, 0);
    if (me == l)
    {
        me->exit_code = exit_code;
        me->leader_exited = 1;
        me->state = PROCESS_STATE_WAITING;
        me->wchan = &me->leader_exited;
        halt_forever();
    }
    if (me->clear_tid)
    {
        uint64_t ph = vmm_virt_to_phys_w(me->pml4, me->clear_tid);
        if (ph)
        {
            *(uint32_t *)ph = 0;
            futex_wake_phys(ph, 1);
        }
    }
    me->state = PROCESS_STATE_TERMINATED;
    me->pid = 0;
    me->sh->refs--;
    if (l->leader_exited && process_nthreads(l) == 1)
    {
        l->leader_exited = 0;
        make_zombie(l);
    }
    halt_forever();
}
void exit_current_process(int exit_code)
{
    group_exit(exit_code, 0);
}
void exit_by_signal(int sig)
{
    group_exit(0, sig);
}
void process_signal_death(process_t *t, int sig)
{
    process_t *l = process_leader(t);
    if (l->sh == processes[current_process_idx].sh)
        group_exit(0, sig);
    kill_threads_except(l, l, l);
    l->exit_code = 0;
    l->exit_signal = sig;
    l->leader_exited = 0;
    make_zombie(l);
}
void process_init_limits(process_t *p)
{
    const uint64_t INF = ~0ULL;
    for (int i = 0; i < 16; i++)
        p->sh->rlim[i][0] = p->sh->rlim[i][1] = INF;
    p->sh->rlim[3][0] = 8 * 1024 * 1024;
    p->sh->rlim[3][1] = INF;
    p->sh->rlim[4][0] = p->sh->rlim[4][1] = 0;
    p->sh->rlim[7][0] = p->sh->rlim[7][1] = MAX_FDS;
}
void process_reap(process_t *p)
{
    if (p->sh->cwd)
    {
        iput((void *)p->sh->cwd);
        p->sh->cwd = 0;
    }
    if (p->sh->root)
    {
        iput((void *)p->sh->root);
        p->sh->root = 0;
    }
    fdt_close_all(p);
    uint64_t *pml4 = p->pml4;
    p->pml4 = 0;
    p->state = PROCESS_STATE_TERMINATED;
    p->pid = 0;
    p->tgid = 0;
    p->ppid = 0;
    p->sh->refs = 0;
    p->sh = 0;
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
    c->sh = sh_alloc();
    if (!c->sh)
    {
        vmm_free_user_space(pml4);
        return -1;
    }
    c->pml4 = pml4;
    c->kernel_stack = kstack_top(idx);
    uint64_t *frame = (uint64_t *)c->kernel_stack - 20;
    for (int i = 0; i < 20; i++)
        frame[i] = regs[i];
    frame[14] = 0;
    c->esp = (uint64_t)frame;
    c->pid = (uint32_t)next_pid++;
    c->tgid = c->pid;
    c->is_thread = 0;
    c->leader_exited = 0;
    c->clear_tid = 0;
    c->futex_key = 0;
    c->futex_woken = 0;
    c->cred = parent->cred;
    c->ppid = parent->tgid;
    c->pgid = parent->pgid;
    c->sid = parent->sid;
    c->exit_code = 0;
    c->sh->heap_start = parent->sh->heap_start;
    c->sh->heap_end = parent->sh->heap_end;
    c->sh->nvmas = parent->sh->nvmas;
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
        c->sh->sigact[s_] = parent->sh->sigact[s_];
    for (int r_ = 0; r_ < 16; r_++)
    {
        c->sh->rlim[r_][0] = parent->sh->rlim[r_][0];
        c->sh->rlim[r_][1] = parent->sh->rlim[r_][1];
    }
    c->wake_tick = 0;
    c->tmo_active = 0;
    c->sh->cwd = parent->sh->cwd;
    c->sh->root = parent->sh->root;
    for (int i = 0; i < parent->sh->nvmas; i++)
        c->sh->vmas[i] = parent->sh->vmas[i];
    for (int i = 0; i < MAX_FDS; i++)
    {
        c->sh->fds[i] = 0;
        c->sh->fd_flags[i] = 0;
    }
    c->state = PROCESS_STATE_READY;
    *child_out = c;
    return (int)c->pid;
}
int process_clone_thread(uint64_t *regs, process_t **child_out)
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
    process_t *c = &processes[idx];
    c->kernel_stack = kstack_top(idx);
    if (!c->kernel_stack)
        return -1;
    uint64_t *frame = (uint64_t *)c->kernel_stack - 20;
    for (int i = 0; i < 20; i++)
        frame[i] = regs[i];
    frame[14] = 0;
    c->esp = (uint64_t)frame;
    c->sh = parent->sh;
    c->sh->refs++;
    c->pml4 = parent->pml4;
    c->pid = (uint32_t)next_pid++;
    c->tgid = parent->tgid;
    c->cred = parent->cred;
    c->ppid = 0;
    c->is_thread = 1;
    c->leader_exited = 0;
    c->clear_tid = 0;
    c->futex_key = 0;
    c->futex_woken = 0;
    c->pgid = parent->pgid;
    c->sid = parent->sid;
    c->exit_code = 0;
    c->exit_signal = 0;
    c->fs_base = parent->fs_base;
    c->wchan = 0;
    c->sigmask = parent->sigmask;
    c->sigpending = 0;
    c->restart_frame = 0;
    c->sigsusp = 0;
    c->saved_mask = 0;
    c->alarm_tick = 0;
    c->alarm_interval = 0;
    c->wake_tick = 0;
    c->tmo_active = 0;
    for (int i_ = 0; i_ < 16; i_++)
        c->comm[i_] = parent->comm[i_];
    process_fpu_save_to(idx);
    c->state = PROCESS_STATE_READY;
    *child_out = c;
    return (int)c->pid;
}
