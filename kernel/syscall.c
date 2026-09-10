#include "syscall.h"
#include "console.h"
#include "types.h"
#include "vbe.h"
#include "stdint.h"
#include "process.h"
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
static uint8_t elf_load_buffer[MAX_ELF_SIZE]
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
#define VMM_USER_FLAGS 0x07
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
extern void exit_current_process(void);
extern int spawn_from_ramfs(
    const char *filename
);
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
static int spawn_from_xv6fs(
    const char *path,
    uint64_t caller_argv,
    uint64_t argc
)
{
    syscall_debug(
        "EXEC: xv6fs lookup\n"
    );
    if (!path)
        return -1;
    struct inode *ip =
        namei((char *)path);
    if (!ip)
    {
        syscall_debug(
            "EXEC: namei FAILED\n"
        );
        return -1;
    }
    syscall_debug(
        "EXEC: file found\n"
    );
    ilock(ip);
    uint32_t size =
        ip->size;
    syscall_debug(
        "EXEC: file locked\n"
    );
    if (size == 0)
    {
        syscall_debug(
            "EXEC: empty file\n"
        );
        iunlock(ip);
        iput(ip);
        return -1;
    }
    if (size > MAX_ELF_SIZE)
    {
        syscall_debug(
            "EXEC: ELF too large\n"
        );
        iunlock(ip);
        iput(ip);
        return -1;
    }
    syscall_debug(
        "EXEC: reading ELF\n"
    );
    int read_bytes =
        readi(
            ip,
            (char *)elf_load_buffer,
            0,
            size
        );
    iunlock(ip);
    iput(ip);
    if (read_bytes != (int)size)
    {
        syscall_debug(
            "EXEC: readi FAILED\n"
        );
        return -1;
    }
    syscall_debug(
        "EXEC: ELF read OK\n"
    );
    elf64_ehdr_min_t *ehdr =
        (elf64_ehdr_min_t *)
        elf_load_buffer;
    if (ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F')
    {
        syscall_debug(
            "EXEC: bad ELF magic\n"
        );
        return -1;
    }
    if (ehdr->e_ident[4] != 2)
    {
        syscall_debug(
            "EXEC: not ELF64\n"
        );
        return -1;
    }
    if (ehdr->e_machine != 62)
    {
        syscall_debug(
            "EXEC: wrong machine\n"
        );
        return -1;
    }
    syscall_debug(
        "EXEC: ELF header OK\n"
    );
    syscall_debug(
        "EXEC: create process\n"
    );
    int pid =
        create_process(0);
    if (pid < 0)
    {
        syscall_debug(
            "EXEC: create_process FAILED\n"
        );
        return -1;
    }
    syscall_debug(
        "EXEC: process created\n"
    );
    process_t *proc =
        get_process_by_pid(
            (uint32_t)pid
        );
    if (!proc)
    {
        syscall_debug(
            "EXEC: get_process FAILED\n"
        );
        return -1;
    }
    syscall_debug(
        "EXEC: loading segments\n"
    );
    if (load_elf_into_process(
            proc,
            elf_load_buffer,
            size
        ) < 0)
    {
        syscall_debug(
            "EXEC: load ELF FAILED\n"
        );
        proc->state =
            PROCESS_STATE_TERMINATED;
        return -1;
    }
    syscall_debug(
        "EXEC: segments loaded\n"
    );
    set_process_entry(
        proc,
        ehdr->e_entry
    );
    syscall_debug(
        "EXEC: entry installed\n"
    );
    if (caller_argv != 0 &&
        argc > 0 &&
        argc < MAX_ARGV)
    {
        uint64_t *caller_argv_arr =
            (uint64_t *)caller_argv;
        char *scratch_ptr =
            argv_scratch;
        char *scratch_end =
            argv_scratch + ARGV_SCRATCH_SIZE;
        uint64_t copied_argc = 0;
        for (uint64_t i = 0; i < argc; i++)
        {
            const char *src =
                (const char *)caller_argv_arr[i];
            if (!src)
                break;
            size_t len = 0;
            while (src[len] && (scratch_ptr + len) < scratch_end)
                len++;
            if ((scratch_ptr + len + 1) >= scratch_end)
                break; 
            for (size_t j = 0; j < len; j++)
                scratch_ptr[j] = src[j];
            scratch_ptr[len] = '\0';
            argv_ptrs[copied_argc] =
                (uint64_t)scratch_ptr;
            scratch_ptr += len + 1;
            copied_argc++;
        }
        argv_ptrs[copied_argc] = 0; 
        set_process_argv(
            proc,
            copied_argc,
            (uint64_t)argv_ptrs
        );
        syscall_debug(
            "EXEC: argv installed\n"
        );
    }
    return pid;
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
            vmm_unmap_page(
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
        case 0:
        {
            int fd =
                (int)arg1;
            char *buf =
                (char *)arg2;
            size_t count =
                (size_t)arg3;
            if (!buf ||
                count == 0)
            {
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            if (fd == 0)
            {
                char c =
                    kgetc();
                if (c == 0)
                {
                    regs[14] = 0;
                }
                else
                {
                    buf[0] = c;
                    regs[14] = 1;
                }
                break;
            }
            if (
                fd >= 3 &&
                fd < 16 &&
                proc &&
                proc->ofiles[fd].ip
            )
            {
                struct inode *ip =
                    (struct inode *)
                    proc->ofiles[fd].ip;
                ilock(ip);
                int r =
                    readi(
                        ip,
                        buf,
                        proc->ofiles[fd].offset,
                        count
                    );
                iunlock(ip);
                if (r > 0)
                {
                    proc->ofiles[fd].offset += r;
                }
                regs[14] =
                    (uint64_t)r;
                break;
            }
            regs[14] =
                (uint64_t)-1;
            break;
        }
        case 1:
        {
            int fd =
                (int)arg1;
            const char *buf =
                (const char *)arg2;
            size_t count =
                (size_t)arg3;
            if (fd == 1 ||
                fd == 2)
            {
                if (!buf)
                {
                    regs[14] =
                        (uint64_t)-1;
                    break;
                }
                kprintn(
                    buf,
                    count,
                    0x00000000
                );
                regs[14] =
                    count;
                break;
            }
            if (
                fd >= 3 &&
                fd < 16 &&
                proc &&
                proc->ofiles[fd].ip
            )
            {
                int flags =
                    proc->ofiles[fd].flags;
                if (!(flags & O_WRONLY) &&
                    !(flags & O_RDWR))
                {
                    regs[14] =
                        (uint64_t)-1;
                    break;
                }
                struct inode *ip =
                    (struct inode *)
                    proc->ofiles[fd].ip;
                ilock(ip);
                uint32_t write_off =
                    proc->ofiles[fd].offset;
                if (flags & O_APPEND)
                {
                    write_off =
                        ip->size;
                }
                int r =
                    writei(
                        ip,
                        (char *)buf,
                        write_off,
                        count
                    );
                iupdate(ip);
                iunlock(ip);
                if (r > 0)
                {
                    proc->ofiles[fd].offset =
                        write_off + r;
                }
                regs[14] =
                    (uint64_t)r;
                break;
            }
            regs[14] =
                (uint64_t)-1;
            break;
        }
        case 2:
        {
            const char *path =
                (const char *)arg1;
            int flags =
                (int)arg2;
            syscall_debug(
                "OPEN: enter\n"
            );
            if (!path ||
                !proc)
            {
                syscall_debug(
                    "OPEN: bad args\n"
                );
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            syscall_debug(
                "OPEN: before FS\n"
            );
            struct inode *ip = 0;
            int created_locked = 0;
            if (flags & O_CREAT)
            {
                syscall_debug(
                    "OPEN: xv6fs_create\n"
                );
                ip =
                    xv6fs_create(
                        (char *)path,
                        T_FILE,
                        0,
                        0
                    );
                if (ip)
                {
                    created_locked = 1;
                }
            }
            else
            {
                syscall_debug(
                    "OPEN: namei\n"
                );
                ip =
                    namei(
                        (char *)path
                    );
                if (ip &&
                    (flags & O_TRUNC))
                {
                    ilock(ip);
                    ip->size = 0;
                    iupdate(ip);
                    iunlock(ip);
                }
            }
            syscall_debug(
                "OPEN: FS returned\n"
            );
            if (!ip)
            {
                syscall_debug(
                    "OPEN: FAILED\n"
                );
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            int fd = -1;
            for (int i = 3;
                 i < 16;
                 i++)
            {
                if (proc->ofiles[i].ip == 0)
                {
                    fd = i;
                    break;
                }
            }
            if (fd == -1)
            {
                if (created_locked)
                {
                    iunlock(ip);
                }
                iput(ip);
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            proc->ofiles[fd].ip =
                ip;
            proc->ofiles[fd].offset =
                0;
            proc->ofiles[fd].flags =
                flags;
            if (created_locked)
            {
                iunlock(ip);
            }
            regs[14] =
                (uint64_t)fd;
            syscall_debug(
                "OPEN: SUCCESS\n"
            );
            break;
        }
        case 3:
        {
            int fd =
                (int)arg1;
            if (
                fd >= 3 &&
                fd < 16 &&
                proc &&
                proc->ofiles[fd].ip
            )
            {
                struct inode *ip =
                    (struct inode *)
                    proc->ofiles[fd].ip;
                iput(ip);
                proc->ofiles[fd].ip = 0;
                proc->ofiles[fd].offset = 0;
                proc->ofiles[fd].flags = 0;
                regs[14] = 0;
            }
            else
            {
                regs[14] =
                    (uint64_t)-1;
            }
            break;
        }
        case 8:
        {
            int fd =
                (int)arg1;
            int64_t offset =
                (int64_t)arg2;
            int whence =
                (int)arg3;
            if (
                fd >= 3 &&
                fd < 16 &&
                proc &&
                proc->ofiles[fd].ip
            )
            {
                struct inode *ip =
                    (struct inode *)
                    proc->ofiles[fd].ip;
                int64_t new_offset =
                    -1;
                ilock(ip);
                switch (whence)
                {
                    case SEEK_SET:
                        new_offset =
                            offset;
                        break;
                    case SEEK_CUR:
                        new_offset =
                            (int64_t)
                            proc->ofiles[fd].offset
                            + offset;
                        break;
                    case SEEK_END:
                        new_offset =
                            (int64_t)
                            ip->size
                            + offset;
                        break;
                }
                iunlock(ip);
                if (new_offset < 0)
                {
                    regs[14] =
                        (uint64_t)-1;
                }
                else
                {
                    proc->ofiles[fd].offset =
                        (uint32_t)new_offset;
                    regs[14] =
                        (uint64_t)new_offset;
                }
            }
            else
            {
                regs[14] =
                    (uint64_t)-1;
            }
            break;
        }
        case 12:
        {
            regs[14] =
                sys_brk(arg1);
            break;
        }
        case 32:
        {
            int oldfd =
                (int)arg1;
            if (
                oldfd >= 3 &&
                oldfd < 16 &&
                proc &&
                proc->ofiles[oldfd].ip
            )
            {
                int newfd =
                    -1;
                for (int i = 3;
                     i < 16;
                     i++)
                {
                    if (
                        proc->ofiles[i].ip
                        == 0
                    )
                    {
                        newfd = i;
                        break;
                    }
                }
                if (newfd != -1)
                {
                    proc->ofiles[newfd].ip =
                        proc->ofiles[oldfd].ip;
                    proc->ofiles[newfd].offset =
                        proc->ofiles[oldfd].offset;
                    proc->ofiles[newfd].flags =
                        proc->ofiles[oldfd].flags;
                    idup(
                        (struct inode *)
                        proc->ofiles[newfd].ip
                    );
                    regs[14] =
                        (uint64_t)newfd;
                }
                else
                {
                    regs[14] =
                        (uint64_t)-1;
                }
            }
            else
            {
                regs[14] =
                    (uint64_t)-1;
            }
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
        case 59:
        {
            const char *filename =
                (const char *)arg1;
            syscall_debug(
                "EXECVE: enter\n"
            );
            if (!filename)
            {
                regs[14] =
                    (uint64_t)-1;
                break;
            }
            syscall_debug(
                "EXECVE: xv6fs\n"
            );
            int res =
                spawn_from_xv6fs(
                    filename,
                    arg2,
                    arg3
                );
            if (res < 0)
            {
                syscall_debug(
                    "EXECVE: trying RAMFS\n"
                );
                res =
                    spawn_from_ramfs(
                        filename
                    );
            }
            regs[14] =
                (uint64_t)res;
            if (res >= 0)
            {
                syscall_debug(
                    "EXECVE: SUCCESS\n"
                );
                kflush();
            }
            else
            {
                syscall_debug(
                    "EXECVE: FAILED\n"
                );
            }
            break;
        }
        case 60:
        {
            syscall_debug(
                "EXIT: process\n"
            );
            exit_current_process();
            break;
        }
        case 61:
        {
            regs[14] =
                (uint64_t)-1;
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
                (uint64_t)-1;
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
