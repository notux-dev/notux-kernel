#include "process.h"
#include "stdint.h"
#include "vmm.h"
#include "serial.h"
#include "pmm.h"
process_t processes[MAX_PROCESSES];
int current_process_idx = 0;
static uint64_t next_pid = 1;
static uint8_t kernel_stacks[MAX_PROCESSES][4096]
    __attribute__((aligned(16)));
static uint8_t user_stacks[MAX_PROCESSES][4096]
    __attribute__((aligned(16)));
extern void set_kernel_stack(uint64_t stack);
void scheduler_init(void)
{
    for (int i = 0;
         i < MAX_PROCESSES;
         i++)
    {
        processes[i].pid = 0;
        processes[i].esp = 0;
        processes[i].kernel_stack = 0;
        processes[i].state =
            PROCESS_STATE_TERMINATED;
        processes[i].pml4 = 0;
        processes[i].heap_start = 0;
        processes[i].heap_end = 0;
        for (int j = 0;
             j < 16;
             j++)
        {
            processes[i].ofiles[j].ip = 0;
            processes[i].ofiles[j].offset = 0;
            processes[i].ofiles[j].flags = 0;
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
    uint64_t kernel_stack_top =
        (uint64_t)
        &kernel_stacks[idx][
            sizeof(kernel_stacks[idx])
        ];
    uint64_t user_stack_top =
        (uint64_t)
        &user_stacks[idx][
            sizeof(user_stacks[idx])
        ];
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
    processes[idx].heap_start = 0;
    processes[idx].heap_end = 0;
    for (int i = 0;
         i < 16;
         i++)
    {
        processes[idx].ofiles[i].ip = 0;
        processes[idx].ofiles[i].offset = 0;
        processes[idx].ofiles[i].flags = 0;
    }
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
}
uint64_t schedule(
    uint64_t current_esp
)
{
    serwrite(
        "SCHEDULE: enter\n"
    );
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
        if (current->state ==
            PROCESS_STATE_RUNNING)
        {
            current->state =
                PROCESS_STATE_READY;
        }
        activate_process(
            next_idx
        );
        serwrite(
            "SCHEDULE: switched\n"
        );
        return processes[
            current_process_idx
        ].esp;
    }
    if (current->state !=
        PROCESS_STATE_TERMINATED)
    {
        current->state =
            PROCESS_STATE_RUNNING;
        return current->esp;
    }
    serwrite(
        "SCHEDULE: no runnable process\n"
    );
    asm volatile("sti");
    for (;;)
    {
        asm volatile("hlt");
    }
}
void exit_current_process(void)
{
    processes[
        current_process_idx
    ].state =
        PROCESS_STATE_TERMINATED;
    serwrite(
        "PROCESS: terminated\n"
    );
    asm volatile("sti");
    for (;;)
    {
        asm volatile("hlt");
    }
}
