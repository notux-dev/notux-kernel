#include "syscalls.h"
#include "syscall.h"
#include "process.h"

static const syscall_fn_t sys_call_table[NR_SYSCALLS] = {
    [NR_read] = sys_read, [NR_write] = sys_write, [NR_open] = sys_open,
    [NR_close] = sys_close, [NR_lseek] = sys_lseek, [NR_dup] = sys_dup,
    [NR_dup2] = sys_dup2, [NR_dup3] = sys_dup3, [NR_fcntl] = sys_fcntl, [NR_pipe] = sys_pipe, [NR_clock_gettime] = sys_clock_gettime, [NR_clock_getres] = sys_clock_getres, [NR_gettimeofday] = sys_gettimeofday, [NR_time] = sys_time, [NR_times] = sys_times, [NR_sched_yield] = sys_sched_yield, [NR_nanosleep] = sys_nanosleep, [NR_clock_nanosleep] = sys_clock_nanosleep, [NR_poll] = sys_poll, [NR_ppoll] = sys_ppoll, [NR_select] = sys_select, [NR_pselect6] = sys_pselect6, [NR_pipe2] = sys_pipe2,
    [NR_mmap]     = sys_mmap,
    [NR_munmap]   = sys_munmap,
    [NR_mprotect] = sys_mprotect,
    [NR_madvise]  = sys_madvise,
    [NR_mremap]   = sys_mremap,
    [NR_msync]    = sys_mem_nop, [NR_mlock] = sys_mem_nop, [NR_munlock] = sys_mem_nop,
    [NR_mlockall] = sys_mem_nop, [NR_munlockall] = sys_mem_nop,
    [NR_mincore]  = sys_mincore,
    [NR_execve]   = sys_execve,
    [NR_arch_prctl] = sys_arch_prctl,
    [NR_set_tid_address] = sys_set_tid_address,
    [NR_gettid]   = sys_gettid,
    [NR_exit_group] = sys_exit_group,
    [NR_getuid]   = sys_getuid,
    [NR_geteuid]  = sys_getuid,
    [NR_getgid]   = sys_getuid,
    [NR_getegid]  = sys_getuid,
    [NR_writev]   = sys_writev,
    [NR_readv]    = sys_readv,
    [NR_ioctl]    = sys_ioctl,
    [NR_stat] = sys_stat,
    [NR_fstat] = sys_fstat,
    [NR_lstat] = sys_lstat,
    [NR_newfstatat] = sys_newfstatat,
    [NR_access] = sys_access,
    [NR_faccessat] = sys_faccessat,
    [NR_getcwd] = sys_getcwd,
    [NR_chdir] = sys_chdir,
    [NR_getdents64] = sys_getdents64,
    [NR_pread64] = sys_pread64,
    [NR_readlink] = sys_readlink,
    [NR_umask] = sys_umask,
    [NR_uname] = sys_uname,
    [NR_openat] = sys_openat,
    [NR_mkdirat] = sys_mkdirat,
    [NR_unlinkat] = sys_unlinkat,
    [NR_setpgid] = sys_setpgid,
    [NR_getpgid] = sys_getpgid, [NR_getsid] = sys_getsid,
    [NR_getpgrp] = sys_getpgrp,
    [NR_setsid] = sys_setsid,
    [NR_readlinkat] = sys_readlinkat,
    [NR_fork]    = sys_fork,
    [NR_clone]   = sys_clone,
    [NR_socket]  = sys_socket, [NR_connect] = sys_notsock, [NR_sendto] = sys_notsock,
    [NR_vfork]   = sys_fork,
    [NR_wait4]   = sys_wait4,
    [NR_getpid]  = sys_getpid,
    [NR_getppid] = sys_getppid,
    [NR_rt_sigaction] = sys_rt_sigaction, [NR_rt_sigprocmask] = sys_rt_sigprocmask,
    [NR_rt_sigpending] = sys_rt_sigpending, [NR_rt_sigsuspend] = sys_rt_sigsuspend, [NR_rt_sigtimedwait] = sys_rt_sigtimedwait,
    [15] = sys_rt_sigreturn, [NR_pause] = sys_pause, [NR_sigaltstack] = sys_sigaltstack,
    [NR_kill] = sys_kill, [NR_tkill] = sys_tkill, [NR_tgkill] = sys_tgkill,
    [NR_getrlimit] = sys_getrlimit, [NR_setrlimit] = sys_setrlimit, [NR_prlimit64] = sys_prlimit64,
    [NR_getrusage] = sys_getrusage, [NR_getrandom] = sys_getrandom, [NR_membarrier] = sys_membarrier,
    [NR_setuid] = sys_ret0, [NR_setgid] = sys_ret0, [NR_setreuid] = sys_ret0, [NR_setregid] = sys_ret0,
    [NR_setresuid] = sys_ret0, [NR_setresgid] = sys_ret0, [NR_setgroups] = sys_ret0,
    [NR_getgroups] = sys_getgroups, [NR_getresuid] = sys_getresid, [NR_getresgid] = sys_getresid,
    [NR_prctl] = sys_ret0, [NR_set_robust_list] = sys_ret0,
    [NR_sched_getaffinity] = sys_sched_getaffinity,
    [NR_alarm] = sys_alarm, [NR_getitimer] = sys_itimer, [NR_setitimer] = sys_itimer,
    [NR_mkdir] = sys_mkdir, [NR_rmdir] = sys_rmdir, [NR_unlink] = sys_unlink,
    [NR_rename] = sys_rename, [NR_renameat] = sys_renameat, [NR_renameat2] = sys_renameat2,
    [NR_link] = sys_link, [NR_linkat] = sys_linkat, [NR_creat] = sys_creat,
    [NR_symlink] = sys_symlink, [NR_symlinkat] = sys_symlinkat, [NR_mknod] = sys_eperm, [NR_mknodat] = sys_eperm,
    [NR_fallocate] = sys_eopnotsupp,
    [NR_ftruncate] = sys_ftruncate, [NR_truncate] = sys_truncate, [NR_pwrite64] = sys_pwrite64,
    [NR_statfs] = sys_statfs, [NR_fstatfs] = sys_fstatfs, [NR_fchdir] = sys_fchdir,
    [NR_fsync] = sys_fsync, [NR_fdatasync] = sys_fsync, [NR_syncfs] = sys_fsync, [NR_sync] = sys_sync,
    [NR_mount] = sys_mount, [NR_umount2] = sys_ret0, [NR_reboot] = sys_reboot,
    [NR_chmod] = sys_chmod, [NR_chown] = sys_chown, [NR_lchown] = sys_lchown, [NR_utimes] = sys_utimes,
    [NR_fchmodat] = sys_fchmodat, [NR_fchownat] = sys_fchownat, [NR_utimensat] = sys_utimensat,
    [NR_futimesat] = sys_futimesat,
    [NR_fchmod] = sys_fchmod, [NR_fchown] = sys_fchown, [NR_flock] = sys_fd_stub, [NR_fadvise64] = sys_fd_stub,
};

extern void xv6fs_bridge_use_cwd(void *ip);

static uint64_t dispatch_inner(uint64_t *regs)
{
    xv6fs_bridge_use_cwd(get_current_process()->cwd);
    uint64_t nr = regs[14];
#ifdef SYSCALL_TRACE
    {
        extern void serwrite(const char *);
        char b[48]; int n = 0;
        uint64_t vals[3] = { get_current_process()->pid, nr, regs[9] };
        for (int i = 0; i < 3; i++)
        {
            char t[20]; int k = 0; uint64_t v = vals[i];
            if (i == 2) { b[n++] = '('; if ((int64_t)v < 0) { b[n++]='-'; v = -(int64_t)v; } }
            do { t[k++] = '0' + v % 10; v /= 10; } while (v);
            while (k) b[n++] = t[--k];
            b[n++] = i == 0 ? ':' : i == 1 ? ' ' : ')';
        }
        b[n++] = '\n'; b[n] = 0;
        serwrite(b);
    }
#endif
    if (nr < NR_SYSCALLS && sys_call_table[nr])
        return sys_call_table[nr](regs);
    if (nr != 12 && nr != 60 && nr != 83 && nr != 87 && nr != 222)
    {
        extern void serwrite(const char *);
        static const char hex[] = "0123456789";
        char b[24] = "ENOSYS nr=";
        int n = 10;
        char t[8]; int k = 0;
        uint64_t v = nr;
        do { t[k++] = hex[v % 10]; v /= 10; } while (v && k < 7);
        while (k) b[n++] = t[--k];
        b[n++] = '\n'; b[n] = 0;
        serwrite(b);
    }
    syscall_handler(regs);
    return (uint64_t)regs;
}

uint64_t syscall_dispatch(uint64_t *regs)
{
    return signal_check((uint64_t *)dispatch_inner(regs));
}
