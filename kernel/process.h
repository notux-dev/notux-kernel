#ifndef PROCESS_H
#define PROCESS_H 
#include <stdint.h>
#define MAX_PROCESSES 256
#define MAX_SHARED 64
#define KERNEL_STACK_SIZE 8192
#define USER_STACK_SIZE 16384
#define USER_STACK_TOP 0x7FFFFFFF0000ULL
#define MAX_VMAS 512
#define MAX_FDS 64
struct kfile;
typedef struct
{
    uint64_t start;
    uint64_t end;
} vma_t;
typedef struct
{
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
} ksigaction_t;
#define NSIG_ 64
typedef enum
{
    PROCESS_STATE_READY = 0,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_WAITING,
    PROCESS_STATE_ZOMBIE,
    PROCESS_STATE_TERMINATED
} process_state_t;
typedef struct pshared
{
    int refs;
    uint64_t heap_start;
    uint64_t heap_end;
    vma_t vmas[MAX_VMAS];
    int nvmas;
    void *cwd;
    void *root;
    ksigaction_t sigact[NSIG_];
    uint64_t rlim[16][2];
    struct kfile *fds[MAX_FDS];
    uint8_t fd_flags[MAX_FDS];
} pshared_t;
#define NGROUPS_MAX_ 32
typedef struct
{
    uint32_t uid, euid, suid;
    uint32_t gid, egid, sgid;
    uint32_t umask;
    int ngroups;
    uint32_t groups[NGROUPS_MAX_];
} cred_t;
typedef struct
{
    uint32_t pid;
    uint32_t tgid;
    uint32_t ppid;
    int exit_code;
    uint64_t esp;
    uint64_t kernel_stack;
    process_state_t state;
    uint64_t *pml4;
    pshared_t *sh;
    cred_t cred;
    uint64_t fs_base;
    uint64_t sigmask;
    uint64_t sigpending;
    int restart_frame;
    int sigsusp;
    uint64_t saved_mask;
    uint64_t alarm_tick;
    uint64_t alarm_interval;
    uint32_t pgid;
    uint32_t sid;
    char comm[16];
    int exit_signal;
    uint64_t wake_tick;
    uint64_t tmo_deadline;
    int tmo_active;
    void *wchan;
    int is_thread;
    int leader_exited;
    uint64_t clear_tid;
    uint64_t futex_key;
    int futex_woken;
} process_t;
extern process_t processes[MAX_PROCESSES];
extern int current_process_idx;
void scheduler_init(void);
process_t *get_current_process(void);
process_t *get_process_by_pid(uint32_t pid);
int create_process(uint64_t entry_point);
void set_process_entry(process_t *proc, uint64_t entry_point);
void set_process_argv(process_t *proc, uint64_t argc, uint64_t argv_ptr);
void exit_current_process(int exit_code);
void exit_by_signal(int sig);
void process_signal_death(process_t *t, int sig);
void process_init_limits(process_t *p);
int process_clone_thread(uint64_t *regs, process_t **child_out);
void thread_exit(int exit_code);
void group_exit(int exit_code, int sig);
int process_nthreads(process_t *p);
process_t *process_leader(process_t *p);
void process_kill_other_threads(process_t *me);
void process_fpu_init(int idx);
void process_fpu_reset_current(void);
void process_fpu_save_to(int idx);
void signal_post(process_t *t, int sig);
uint64_t signal_check(uint64_t *frame);
uint64_t schedule(uint64_t current_esp);
void process_reap(process_t *p);
uint64_t sleep_on(uint64_t *regs, uint64_t nr, void *chan);
uint64_t sleep_until(uint64_t *regs, uint64_t nr, void *chan, uint64_t wake_tick);
void wakeup(void *chan);
int process_fork(uint64_t *regs, process_t **child_out);
#endif
