#include "syscalls.h"
#include "kfile.h"
#include "process.h"
#include "vmm.h"
#include "io.h"
#include "uaccess.h"
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
extern struct inode *idup(struct inode *ip);

uint64_t sys_getpid(uint64_t *regs)
{
    regs[14] = get_current_process()->pid;
    return (uint64_t)regs;
}

uint64_t sys_getppid(uint64_t *regs)
{
    regs[14] = get_current_process()->ppid;
    return (uint64_t)regs;
}

uint64_t sys_fork(uint64_t *regs)
{
    process_t *parent = get_current_process();
    process_t *child = 0;
    int pid = process_fork(regs, &child);
    if (pid < 0)
    {
        regs[14] = (uint64_t)-EAGAIN;
        return (uint64_t)regs;
    }
    fdt_clone(parent, child);
    if (parent->cwd)
        idup((struct inode *)parent->cwd);
    regs[14] = (uint64_t)pid;
    return (uint64_t)regs;
}

#define CL_VM        0x00000100UL
#define CL_FS        0x00000200UL
#define CL_FILES     0x00000400UL
#define CL_SIGHAND   0x00000800UL
#define CL_VFORK     0x00004000UL
#define CL_SETTLS    0x00080000UL
#define CL_PSETTID   0x00100000UL
#define CL_CSETTID   0x01000000UL
#define CL_CCLEARTID 0x00200000UL
uint64_t sys_clone(uint64_t *regs)
{
    uint64_t flags = regs[9], stack = regs[10], ptid = regs[11], ctid = regs[5], tls = regs[7];
    uint64_t ok = 0xFFUL | CL_VM | CL_FS | CL_FILES | CL_SIGHAND | CL_VFORK |
                  CL_SETTLS | CL_PSETTID | CL_CSETTID | CL_CCLEARTID;
    if (flags & ~ok)
    {
        regs[14] = (uint64_t)-EINVAL;
        return (uint64_t)regs;
    }
    process_t *parent = get_current_process();
    process_t *child = 0;
    int pid = process_fork(regs, &child);
    if (pid < 0)
    {
        regs[14] = (uint64_t)-EAGAIN;
        return (uint64_t)regs;
    }
    fdt_clone(parent, child);
    if (parent->cwd)
        idup((struct inode *)parent->cwd);
    if (stack)
        ((uint64_t *)child->esp)[18] = stack;
    if (flags & CL_SETTLS)
        child->fs_base = tls;
    if (flags & CL_PSETTID)
        copy_to_user(parent, ptid, &pid, sizeof pid);
    if (flags & CL_CSETTID)
    {
        uint64_t ph = vmm_virt_to_phys(child->pml4, ctid);
        if (ph)
            *(int *)ph = pid;
    }
    regs[14] = (uint64_t)pid;
    return (uint64_t)regs;
}

#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

uint64_t sys_mount(uint64_t *regs)
{
    process_t *p = get_current_process();
    char fs[16];
    if (strncpy_from_user(p, fs, regs[11], sizeof fs) < 0)
        RET(-EFAULT);
    static const char *ok[] = {"tmpfs", "proc", "sysfs", "devtmpfs", "devpts", "cgroup", "cgroup2", "none", 0};
    for (int i = 0; ok[i]; i++)
        if (streq(fs, ok[i]))
            RET(0);
    RET(-ENODEV);
}

uint64_t sys_reboot(uint64_t *regs)
{
    if (regs[11] == 0x01234567 || regs[11] == 0xCDEF0123 || regs[11] == 0x4321FEDC || regs[11] == 0xA1B2C3D4)
    {
        sys_sync(regs);
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);
        for (;;)
            asm volatile("hlt");
    }
    RET(0);
}

uint64_t sys_socket(uint64_t *regs) { regs[14] = (uint64_t)-EAFNOSUPPORT; return (uint64_t)regs; }
uint64_t sys_notsock(uint64_t *regs) { regs[14] = (uint64_t)-ENOTSOCK; return (uint64_t)regs; }

#include "vmm.h"
#include "numbers.h"
#include "uaccess.h"

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

uint64_t sys_arch_prctl(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t code = regs[9], addr = regs[10];
    if (code == ARCH_SET_FS)
    {
        if (addr > 0x7FFFFFFFFFFFULL)
        {
            regs[14] = (uint64_t)-EINVAL;
            return (uint64_t)regs;
        }
        p->fs_base = addr;
        __asm__ volatile("wrmsr" : : "c"(0xC0000100u),
                         "a"((uint32_t)addr), "d"((uint32_t)(addr >> 32)));
        regs[14] = 0;
    }
    else if (code == ARCH_GET_FS)
    {
        uint64_t phys = addr <= 0x7FFFFFFFFFF0ULL
                        ? vmm_virt_to_phys(p->pml4, addr) : 0;
        if (!phys)
            regs[14] = (uint64_t)-EFAULT;
        else
        {
            *(uint64_t *)phys = p->fs_base;
            regs[14] = 0;
        }
    }
    else
        regs[14] = (uint64_t)-EINVAL;
    return (uint64_t)regs;
}

uint64_t sys_set_tid_address(uint64_t *regs)
{
    regs[14] = get_current_process()->pid;
    return (uint64_t)regs;
}

uint64_t sys_gettid(uint64_t *regs)
{
    regs[14] = get_current_process()->pid;
    return (uint64_t)regs;
}

uint64_t sys_exit_group(uint64_t *regs)
{
    exit_current_process((int)regs[9]);
    return (uint64_t)regs;
}

uint64_t sys_getuid(uint64_t *regs)
{
    regs[14] = 0;
    return (uint64_t)regs;
}
