#include "process.h"
#include "stdint.h"
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
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        processes[i].pid = 0;
        processes[i].esp = 0;
        processes[i].kernel_stack = 0;
        processes[i].state = PROCESS_STATE_TERMINATED;
        processes[i].pml4 = 0;
        processes[i].heap_start = 0;
        processes[i].heap_end = 0;
        for (int j = 0; j < 16; j++)
        {
            processes[i].ofiles[j].ip = 0;
            processes[i].ofiles[j].offset = 0;
            processes[i].ofiles[j].flags = 0;
        }
    }
    current_process_idx = 0;
    next_pid = 1;
}
process_t *get_current_process(void)
{
    return &processes[current_process_idx];
}
int create_process(uint64_t entry_point)
{
    int idx = -1;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processes[i].state == PROCESS_STATE_TERMINATED)
        {
            idx = i;
            break;
        }
    }
    if (idx == -1)
        return -1;
    uint64_t kernel_stack_top =
        (uint64_t)&kernel_stacks[idx][4096];
    uint64_t user_stack_top =
        (uint64_t)&user_stacks[idx][4096];
    processes[idx].kernel_stack = kernel_stack_top;
    uint64_t *stack = (uint64_t *)kernel_stack_top;
    *(--stack) = 0x1B;
    *(--stack) = user_stack_top;
    *(--stack) = 0x202;
    *(--stack) = 0x23;
    *(--stack) = entry_point;
    for (int i = 0; i < 15; i++)
    {
        *(--stack) = 0;
    }
    processes[idx].esp = (uint64_t)stack;
    processes[idx].pid = (uint32_t)next_pid++;
    processes[idx].pml4 = 0;
    processes[idx].heap_start = 0;
    processes[idx].heap_end = 0;
    for (int i = 0; i < 16; i++)
    {
        processes[idx].ofiles[i].ip = 0;
        processes[idx].ofiles[i].offset = 0;
        processes[idx].ofiles[i].flags = 0;
    }
    processes[idx].state = PROCESS_STATE_READY;
    return processes[idx].pid;
}
uint64_t schedule(uint64_t current_esp)
{
    if (processes[current_process_idx].state == PROCESS_STATE_RUNNING)
    {
        processes[current_process_idx].esp = current_esp;
        processes[current_process_idx].state = PROCESS_STATE_READY;
    }
    int next_idx = -1;
    for (int i = 1; i <= MAX_PROCESSES; i++)
    {
        int idx =
            (current_process_idx + i) % MAX_PROCESSES;
        if (processes[idx].state == PROCESS_STATE_READY)
        {
            next_idx = idx;
            break;
        }
    }
    if (next_idx == -1)
    {
        if (processes[current_process_idx].state !=
            PROCESS_STATE_TERMINATED)
        {
            processes[current_process_idx].state =
                PROCESS_STATE_RUNNING;
            return processes[current_process_idx].esp;
        }
        asm volatile ("sti");
        for (;;)
        {
            asm volatile ("hlt");
        }
    }
    current_process_idx = next_idx;
    processes[current_process_idx].state =
        PROCESS_STATE_RUNNING;
    set_kernel_stack(
        processes[current_process_idx].kernel_stack
    );
    return processes[current_process_idx].esp;
}
void exit_current_process(void)
{
    processes[current_process_idx].state =
        PROCESS_STATE_TERMINATED;
    asm volatile ("sti");
    for (;;)
    {
        asm volatile ("hlt");
    }
}
