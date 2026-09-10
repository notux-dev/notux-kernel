#ifndef PROCESS_H
#define PROCESS_H
#include <stdint.h>
#define MAX_PROCESSES 4
typedef enum
{
    PROCESS_STATE_READY = 0,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_TERMINATED
} process_state_t;
typedef struct
{
    uint32_t pid;
    uint64_t esp;
    uint64_t kernel_stack;
    process_state_t state;
    uint64_t *pml4;
    uint64_t heap_start;
    uint64_t heap_end;
    struct
    {
        void *ip;
        uint32_t offset;
        int flags;
    } ofiles[16];
} process_t;
extern process_t processes[MAX_PROCESSES];
extern int current_process_idx;
void scheduler_init(void);
process_t *get_current_process(void);
process_t *get_process_by_pid(uint32_t pid);
int create_process(uint64_t entry_point);
void set_process_entry(process_t *proc, uint64_t entry_point);
void set_process_argv(process_t *proc, uint64_t argc, uint64_t argv_ptr);
void exit_current_process(void);
uint64_t schedule(uint64_t current_esp);
#endif
