#include "syscall.h"
#include "console.h"
#include "types.h"
#include "vbe.h"
#include "stdint.h"
#include "process.h"
#include "vmm.h"
#include "serial.h"
#include "Xjail/xv6/xv6fs_api.h"
extern void xv6fs_bridge_set_cwd(struct inode *ip);
extern int create_process(uint64_t entry_point);
extern int load_elf_into_process(
    process_t *proc,
    const uint8_t *file,
    uint64_t file_size
);
extern process_t *get_process_by_pid(
    uint32_t pid
);
extern void set_process_entry(
    process_t *proc,
    uint64_t entry_point
);
#define MAX_ELF_SIZE (256 * 1024)
#define MAX_ARGV 16
#define ARGV_SCRATCH_SIZE 4096
static char argv_scratch[ARGV_SCRATCH_SIZE];
static uint64_t argv_ptrs[MAX_ARGV];
uint8_t elf_load_buffer[MAX_ELF_SIZE]
    __attribute__((aligned(4096)));
typedef struct
{
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
} __attribute__((packed))
elf64_ehdr_min_t;
#ifndef _UINT_DEFINED
#define _UINT_DEFINED
typedef unsigned int uint;
#endif
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "Xjail/xv6/xv6fs_api.h"
typedef uint64_t size_t;
#define PAGE_SIZE 4096
#define PAGE_ALIGN_UP(x) \
    (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(x) \
    ((x) & ~(PAGE_SIZE - 1))
#define VMM_USER_FLAGS (0x07 | (1 << 9))
#ifndef O_RDONLY
#define O_RDONLY 0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY 0x0001
#endif
#ifndef O_RDWR
#define O_RDWR   0x0002
#endif
#ifndef O_CREAT
#define O_CREAT  0x0040
#endif
#ifndef O_TRUNC
#define O_TRUNC  0x0200
#endif
#ifndef O_APPEND
#define O_APPEND 0x0400
#endif
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif
#ifndef T_DIR
#define T_DIR 1
#endif
#ifndef T_FILE
#define T_FILE 2
#endif
extern char kgetc(void);
extern void kflush(void);
extern process_t *
create_user_process_from_memory(
    const void *elf_data,
    size_t size
);
extern void *pmm_alloc_page(void);
extern void pmm_free_page(
    void *ptr
);
extern void vmm_map_page(
    uint64_t *pml4,
    uint64_t virt,
    uint64_t phys,
    uint64_t flags
);
extern void vmm_unmap_page(
    uint64_t *pml4,
    uint64_t virt
);
extern struct inode *idup(
    struct inode *
);
static void syscall_debug(
    const char *text
)
{
    serwrite(text);
}
static uint64_t sys_brk(
    uint64_t new_brk
)
{
    process_t *proc =
        get_current_process();
    if (!proc)
        return 0;
    if (proc->heap_start == 0)
    {
        proc->heap_start =
            0x700000000000ULL;
        proc->heap_end =
            proc->heap_start;
    }
    if (new_brk == 0 ||
        new_brk == proc->heap_end)
    {
        return proc->heap_end;
    }
    if (new_brk < proc->heap_start)
    {
        return proc->heap_end;
    }
    uint64_t current_page_end =
        PAGE_ALIGN_UP(
            proc->heap_end
        );
    uint64_t new_page_end =
        PAGE_ALIGN_UP(
            new_brk
        );
    if (new_page_end >
        current_page_end)
    {
        for (
            uint64_t page =
                current_page_end;
            page < new_page_end;
            page += PAGE_SIZE
        )
        {
            void *phys =
                pmm_alloc_page();
            if (!phys)
                return proc->heap_end;
            for (int zi = 0; zi < PAGE_SIZE; zi++)
                ((uint8_t *)phys)[zi] = 0;
            vmm_map_page(
                proc->pml4,
                page,
                (uint64_t)phys,
                VMM_USER_FLAGS
            );
        }
    }
    else if (
        new_page_end <
        current_page_end
    )
    {
        for (
            uint64_t page =
                new_page_end;
            page < current_page_end;
            page += PAGE_SIZE
        )
        {
            vmm_free_page(
                proc->pml4,
                page
            );
        }
    }
    proc->heap_end =
        new_brk;
    return proc->heap_end;
}
void syscall_handler(
    uint64_t *regs
)
{
    uint64_t num =
        regs[14];
    uint64_t arg1 =
        regs[9];
    uint64_t arg2 =
        regs[10];
    uint64_t arg3 =
        regs[11];
    process_t *proc =
        get_current_process();
    switch (num)
    {
        case 12:
        {
            regs[14] =
                sys_brk(arg1);
            break;
        }
        case 39:
        {
            regs[14] =
                proc
                ? (uint64_t)proc->pid
                : 0;
            break;
        }
        case 57:
        {
            regs[14] =
                (uint64_t)-1;
            break;
        }
        case 60:
        {
            syscall_debug(
                "EXIT: process\n"
            );
            exit_current_process((int)arg1);
            break;
        }
        case 61:
        {
            int found = -1;
            if (proc)
            {
                for (int i = 0;
                     i < MAX_PROCESSES;
                     i++)
                {
                    if (processes[i].ppid == proc->pid &&
                        processes[i].state ==
                            PROCESS_STATE_ZOMBIE)
                    {
                        found = i;
                        break;
                    }
                }
            }
            if (found < 0)
            {
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            int *status_ptr =
                (int *)arg2;
            if (status_ptr)
            {
                *status_ptr =
                    processes[found].exit_code;
            }
            uint32_t child_pid =
                processes[found].pid;
            processes[found].state =
                PROCESS_STATE_TERMINATED;
            processes[found].pid = 0;
            processes[found].ppid = 0;
            processes[found].exit_code = 0;
            regs[14] =
                (uint64_t)child_pid;
            break;
        }
        case 80:
        {
            const char *path =
                (const char *)arg1;
            if (!path)
            {
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            struct inode *ip =
                namei(
                    (char *)path
                );
            if (!ip)
            {
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            ilock(ip);
            if (ip->type != T_DIR)
            {
                iunlock(ip);
                iput(ip);
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            iunlock(ip);
            xv6fs_bridge_set_cwd(
                ip
            );
            regs[14] = 0;
            break;
        }
        case 83:
        {
            const char *path =
                (const char *)arg1;
            if (!path)
            {
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            syscall_debug(
                "MKDIR: enter\n"
            );
            struct inode *ip =
                xv6fs_create(
                    (char *)path,
                    T_DIR,
                    0,
                    0
                );
            if (ip)
            {
                iunlock(ip);
                iput(ip);
                regs[14] = 0;
            }
            else
            {
                regs[14] =
                    (uint64_t)-1;
            }
            syscall_debug(
                "MKDIR: return\n"
            );
            break;
        }
        case 87:
        {
            const char *path =
                (const char *)arg1;
            if (!path)
            {
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            regs[14] =
                (uint64_t)
                xv6fs_unlink(
                    (char *)path
                );
            break;
        }
        case 222:
        {
            const uint32_t *user_buf =
                (const uint32_t *)arg1;
            uint32_t width =
                (uint32_t)arg2;
            uint32_t height =
                (uint32_t)arg3;
            if (
                user_buf &&
                width <= 800 &&
                height <= 600
            )
            {
                volatile uint64_t *fb =
                    (volatile uint64_t *)
                    (uintptr_t)
                    vbeaddr();
                const uint64_t *src =
                    (const uint64_t *)
                    user_buf;
                if (fb)
                {
                    uint32_t total_pixels =
                        width * height;
                    uint32_t count64 =
                        total_pixels / 2;
                    for (
                        uint32_t i = 0;
                        i < count64;
                        i++
                    )
                    {
                        fb[i] =
                            src[i];
                    }
                    if (
                        total_pixels % 2 != 0
                    )
                    {
                        (
                            (volatile uint32_t *)
                            fb
                        )[
                            total_pixels - 1
                        ] =
                            (
                                (const uint32_t *)
                                src
                            )[
                                total_pixels - 1
                            ];
                    }
                }
            }
            regs[14] = 0;
            break;
        }
        default:
        {
            regs[14] =
                (uint64_t)-38;
            break;
        }
    }
}
void syscall_init(void)
{
    extern void iset(
        int num,
        uint64_t base,
        uint16_t sel,
        uint8_t flags
    );
    extern void syscall_handler_stub(void);
    iset(
        0x80,
        (uint64_t)
        syscall_handler_stub,
        0x08,
        0xEE
    );
    serwrite(
        "SYSCALL: initialized\n"
    );
}
