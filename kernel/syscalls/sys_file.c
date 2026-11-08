#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "syscall.h"
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "console.h"
#include "keyboard.h"
int kbd_has_char(void);
#include "Xjail/xv6/xv6fs_api.h"
#include "kfile.h"
#include "ext2/ext2.h"
#include "procfs.h"
#include "tty.h"
extern uint64_t krandom_u64(void);


extern void xv6fs_bridge_use_cwd(void *ip);
extern struct inode *idup(struct inode *ip);

#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)
#define PATH_MAX 512
#define NFILE 256

static struct kfile file_pool[NFILE];

struct kfile *kfile_alloc(int type, int flags, struct inode *ip)
{
    for (int i = 0; i < NFILE; i++)
        if (file_pool[i].refs == 0)
        {
            struct kfile *f = &file_pool[i];
            f->refs = 1;
            f->type = type;
            f->flags = flags;
            f->off = 0;
            f->ip = ip;
            f->pipe = 0;
            f->pbuf = 0;
            f->plen = 0;
            f->pkind = 0;
            f->ppid = 0;
            return f;
        }
    return 0;
}

void kfile_put(struct kfile *f)
{
    if (--f->refs > 0)
        return;
    if (f->type == FT_INODE && f->ip)
        iput(f->ip);
    if (f->type == FT_PIPE_R || f->type == FT_PIPE_W)
        pipe_close_end(f);
    if (f->type == FT_PROC)
        proc_release(f);
    f->ip = 0;
    f->pipe = 0;
    f->type = FT_NONE;
    f->refs = 0;
}

struct kfile *fd_get(process_t *p, int64_t fd)
{
    if (fd < 0 || fd >= MAX_FDS)
        return 0;
    return p->fds[fd];
}

struct inode *fd_inode_of(process_t *p, int64_t fd)
{
    struct kfile *f = fd_get(p, fd);
    return (f && f->type == FT_INODE) ? f->ip : 0;
}

uint64_t *fd_offset(process_t *p, int64_t fd)
{
    struct kfile *f = fd_get(p, fd);
    return f ? &f->off : 0;
}

void fdt_init(process_t *p)
{
    for (int i = 0; i < MAX_FDS; i++)
    {
        p->fds[i] = 0;
        p->fd_flags[i] = 0;
    }
    p->fds[0] = kfile_alloc(FT_CONSOLE, 2, 0);
    p->fds[1] = kfile_alloc(FT_CONSOLE, 2, 0);
    p->fds[2] = kfile_alloc(FT_CONSOLE, 2, 0);
}

void fdt_clone(process_t *parent, process_t *child)
{
    for (int i = 0; i < MAX_FDS; i++)
    {
        child->fds[i] = parent->fds[i];
        child->fd_flags[i] = parent->fd_flags[i];
        if (child->fds[i])
            child->fds[i]->refs++;
    }
}

void fdt_close_all(process_t *p)
{
    for (int i = 0; i < MAX_FDS; i++)
        if (p->fds[i])
        {
            kfile_put(p->fds[i]);
            p->fds[i] = 0;
            p->fd_flags[i] = 0;
        }
}

void fdt_close_cloexec(process_t *p)
{
    for (int i = 0; i < MAX_FDS; i++)
        if (p->fds[i] && (p->fd_flags[i] & FD_CLOEXEC))
        {
            kfile_put(p->fds[i]);
            p->fds[i] = 0;
            p->fd_flags[i] = 0;
        }
}

int fd_install(process_t *p, struct kfile *f, int minfd, int cloexec)
{
    for (int i = minfd; i < MAX_FDS; i++)
        if (!p->fds[i])
        {
            p->fds[i] = f;
            p->fd_flags[i] = cloexec ? FD_CLOEXEC : 0;
            return i;
        }
    return -EMFILE;
}

int path_last_too_long(const char *path)
{
    int n = 0, last = 0;
    for (const char *p = path; ; p++)
    {
        if (*p == '/' || *p == 0)
        {
            if (n)
                last = n;
            n = 0;
            if (*p == 0)
                break;
        }
        else
            n++;
    }
    if (last > 255)
        return 1;
    if (last <= 14)
        return 0;
    extern struct inode *nameiparent(char *path, char *name);
    char nm[16], tmp[512];
    int i = 0;
    for (; path[i] && i < 511; i++)
        tmp[i] = path[i];
    tmp[i] = 0;
    struct inode *dp = nameiparent(tmp, nm);
    if (!dp)
        return 0;
    int on_ext2 = dp->dev == EXT2_DEV;
    iput(dp);
    return !on_ext2;
}


#define CHUNK 512

static int64_t file_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    if ((f->flags & KO_ACCMODE) == KO_WRONLY)
        return -EBADF;
    if (n == 0)
        return 0;
    if (f->type == FT_PIPE_R)
        return pipe_read(p, f, ubuf, n);
    if (f->type == FT_PIPE_W)
        return -EBADF;
    if (f->type == FT_PROC)
        return proc_read(p, f, ubuf, n);
    if (f->type == FT_CONSOLE)
    {
        void *ch = 0;
        (void)ch;
        return tty_read(p, f, ubuf, n, &ch);
    }
    if (f->type == FT_NULL)
        return 0;
    if (f->type == FT_ZERO || f->type == FT_RANDOM)
    {
        uint8_t zb[256];
        int64_t done = 0;
        while ((uint64_t)done < n)
        {
            uint64_t c = n - done > sizeof zb ? sizeof zb : n - done;
            for (uint64_t i = 0; i < c; i++)
                zb[i] = f->type == FT_ZERO ? 0 : (uint8_t)krandom_u64();
            if (copy_to_user(p, ubuf + done, zb, c) < 0)
                return done ? done : -EFAULT;
            done += c;
        }
        return done;
    }
    struct inode *ip = f->ip;
    char buf[CHUNK];
    int64_t total = 0;
    ilock(ip);
    if (ip->type == T_DIR)
    {
        iunlock(ip);
        return -EISDIR;
    }
    while ((uint64_t)total < n)
    {
        uint64_t want = n - total;
        if (want > CHUNK)
            want = CHUNK;
        int r = readi(ip, buf, (uint)f->off, (uint)want);
        if (r <= 0)
            break;
        if (copy_to_user(p, ubuf + total, buf, r) < 0)
        {
            iunlock(ip);
            return total ? total : -EFAULT;
        }
        f->off += r;
        total += r;
        if ((uint64_t)r < want)
            break;
    }
    iunlock(ip);
    return total;
}

static int64_t file_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    if ((f->flags & KO_ACCMODE) == 0)
        return -EBADF;
    if (n == 0)
        return 0;
    char buf[CHUNK];
    int64_t total = 0;
    if (f->type == FT_PIPE_W)
        return pipe_write(p, f, ubuf, n);
    if (f->type == FT_PIPE_R)
        return -EBADF;
    if (f->type == FT_CONSOLE)
        return tty_write(p, ubuf, n);
    if (f->type == FT_NULL || f->type == FT_ZERO || f->type == FT_RANDOM)
        return (int64_t)n;
    struct inode *ip = f->ip;
    ilock(ip);
    if (ip->type == T_DIR)
    {
        iunlock(ip);
        return -EISDIR;
    }
    while ((uint64_t)total < n)
    {
        uint64_t want = n - total;
        if (want > CHUNK)
            want = CHUNK;
        if (copy_from_user(p, buf, ubuf + total, want) < 0)
        {
            iunlock(ip);
            return total ? total : -EFAULT;
        }
        if (f->flags & KO_APPEND)
            f->off = ip->size;
        int r = writei(ip, buf, (uint)f->off, (uint)want);
        if (r <= 0)
            break;
        f->off += r;
        total += r;
        if ((uint64_t)r < want)
            break;
    }
    iupdate(ip);
    iunlock(ip);
    return total ? total : -ENOSPC;
}

int64_t kfd_rw(process_t *p, int64_t fd, int wr, uint64_t ubuf, uint64_t n, void **chan)
{
    struct kfile *f = fd_get(p, fd);
    if (!f)
        return -EBADF;
    f->refs++;
    int64_t r = wr ? file_write(p, f, ubuf, n) : file_read(p, f, ubuf, n);
    *chan = f->type == FT_CONSOLE ? (void *)&tty_chan : (void *)f->pipe;
    kfile_put(f);
    return r;
}

uint64_t sys_read(uint64_t *regs)
{
    void *chan = 0;
    int64_t r = kfd_rw(get_current_process(), (int64_t)regs[9], 0, regs[10], regs[11], &chan);
    if (r == E_BLOCK)
        return sleep_on(regs, NR_read, chan);
    RET(r);
}

uint64_t sys_write(uint64_t *regs)
{
    void *chan = 0;
    int64_t r = kfd_rw(get_current_process(), (int64_t)regs[9], 1, regs[10], regs[11], &chan);
    if (r == E_BLOCK)
        return sleep_on(regs, NR_write, chan);
    RET(r);
}

uint64_t sys_close(uint64_t *regs)
{
    process_t *p = get_current_process();
    int64_t fd = (int64_t)regs[9];
    struct kfile *f = fd_get(p, fd);
    if (!f)
        RET(-EBADF);
    p->fds[fd] = 0;
    p->fd_flags[fd] = 0;
    kfile_put(f);
    RET(0);
}

uint64_t sys_lseek(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct kfile *f = fd_get(p, (int64_t)regs[9]);
    int64_t off = (int64_t)regs[10];
    int whence = (int)regs[11];
    if (!f)
        RET(-EBADF);
    if (f->type == FT_PROC)
    {
        if (whence == 0 && off >= 0)
            f->off = (uint64_t)off;
        else if (whence == 1 && (int64_t)f->off + off >= 0)
            f->off += off;
        else if (whence == 2 && !proc_f_is_dir(f) && (int64_t)f->plen + off >= 0)
            f->off = (uint64_t)((int64_t)f->plen + off);
        else
            RET(-EINVAL);
        RET(f->off);
    }
    if (f->type != FT_INODE)
        RET(-ESPIPE);
    int64_t base;
    if (whence == 0)
        base = 0;
    else if (whence == 1)
        base = (int64_t)f->off;
    else if (whence == 2)
    {
        ilock(f->ip);
        base = f->ip->size;
        iunlock(f->ip);
    }
    else
        RET(-EINVAL);
    int64_t np = base + off;
    if (np < 0)
        RET(-EINVAL);
    f->off = (uint64_t)np;
    RET(np);
}


static int64_t do_open(process_t *p, int64_t dirfd, uint64_t upath, int flags, uint64_t mode)
{
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, upath, PATH_MAX) < 0)
        return -EFAULT;
    if (path[0] == 0)
        return -ENOENT;
    if (path[0] == '/')
    {
        int64_t pr = proc_open(p, path, flags);
        if (pr != PROC_NOTMINE)
            return pr;
    }

    struct inode *dir = 0;
    if (path[0] != '/' && dirfd != AT_FDCWD)
    {
        dir = fd_inode_of(p, dirfd);
        if (!dir)
            return fd_get(p, dirfd) ? -ENOTDIR : -EBADF;
        xv6fs_bridge_use_cwd(dir);
    }

    struct inode *ip = 0;
    int locked = 0;
    int64_t err = 0;
    if (flags & KO_CREAT)
    {
        struct inode *e = namei(path);
        if (e && (flags & O_EXCL))
        {
            iput(e);
            err = -EEXIST;
            goto out;
        }
        if (e)
            ip = e;
        else
        {
            if (path_last_too_long(path))
            {
                err = -ENAMETOOLONG;
                goto out;
            }
            extern int xv6fs_create_mode, xv6fs_errno;
            xv6fs_create_mode = (int)(mode & ~022u);
            ip = xv6fs_create(path, T_FILE, 0, 0);
            xv6fs_create_mode = -1;
            if (ip)
                locked = 1;
            else
            {
                err = xv6fs_errno ? xv6fs_errno : -ENOENT;
                goto out;
            }
        }
    }
    else
        ip = namei(path);
    if (!ip)
    {
        extern int namei_enotdir;
        err = namei_enotdir ? -ENOTDIR : -ENOENT;
        goto out;
    }
    if (!locked)
        ilock(ip);
    if (ip->type == T_DEV)
    {
        int mj = ip->major, mn = ip->minor;
        iunlock(ip);
        iput(ip);
        int ft = 0;
        if (mj == 1 && mn == 3) ft = FT_NULL;
        else if (mj == 1 && mn == 5) ft = FT_ZERO;
        else if (mj == 1 && (mn == 8 || mn == 9)) ft = FT_RANDOM;
        else if (mj == 4 || mj == 5) ft = FT_CONSOLE;
        if (!ft)
        {
            err = -ENXIO;
            goto out;
        }
        struct kfile *df = kfile_alloc(ft, flags & (KO_ACCMODE | O_NONBLOCK), 0);
        if (!df)
        {
            err = -ENFILE;
            goto out;
        }
        df->pkind = (mj << 8) | mn;
        int dfd = fd_install(p, df, 0, (flags & O_CLOEXEC) != 0);
        if (dfd < 0)
        {
            kfile_put(df);
            err = dfd;
            goto out;
        }
        err = dfd;
        goto out;
    }
    if (ip->type == T_DIR &&
        ((flags & KO_ACCMODE) != 0 || (flags & KO_TRUNC)))
    {
        iunlock(ip);
        iput(ip);
        err = -EISDIR;
        goto out;
    }
    if ((flags & O_DIRECTORY) && ip->type != T_DIR)
    {
        iunlock(ip);
        iput(ip);
        err = -ENOTDIR;
        goto out;
    }
    if ((flags & KO_TRUNC) && ip->type == T_FILE)
    {
        xv6fs_truncate(ip, 0);
    }
    iunlock(ip);

    struct kfile *f = kfile_alloc(FT_INODE,
                                  flags & (KO_ACCMODE | KO_APPEND | O_NONBLOCK), ip);
    if (!f)
    {
        iput(ip);
        err = -ENFILE;
        goto out;
    }
    int fd = fd_install(p, f, 0, (flags & O_CLOEXEC) != 0);
    if (fd < 0)
    {
        kfile_put(f);
        err = fd;
        goto out;
    }
    err = fd;
out:
    if (dir)
        xv6fs_bridge_use_cwd(p->cwd);
    return err;
}

uint64_t sys_open(uint64_t *regs)
{
    RET(do_open(get_current_process(), AT_FDCWD, regs[9], (int)regs[10], regs[11]));
}

uint64_t sys_creat(uint64_t *regs)
{
    RET(do_open(get_current_process(), AT_FDCWD, regs[9], KO_CREAT | KO_TRUNC | KO_WRONLY, regs[10]));
}

uint64_t sys_openat(uint64_t *regs)
{
    RET(do_open(get_current_process(), (int64_t)regs[9], regs[10], (int)regs[11], regs[5]));
}


static int64_t do_dup(process_t *p, int64_t oldfd, int minfd, int cloexec)
{
    struct kfile *f = fd_get(p, oldfd);
    if (!f)
        return -EBADF;
    f->refs++;
    int fd = fd_install(p, f, minfd, cloexec);
    if (fd < 0)
        kfile_put(f);
    return fd;
}

static int64_t do_dup_to(process_t *p, int64_t oldfd, int64_t newfd, int cloexec)
{
    struct kfile *f = fd_get(p, oldfd);
    if (!f)
        return -EBADF;
    if (newfd < 0 || newfd >= MAX_FDS)
        return -EBADF;
    if (oldfd == newfd)
        return newfd;
    f->refs++;
    struct kfile *old = p->fds[newfd];
    p->fds[newfd] = f;
    p->fd_flags[newfd] = cloexec ? FD_CLOEXEC : 0;
    if (old)
        kfile_put(old);
    return newfd;
}

uint64_t sys_dup(uint64_t *regs)
{
    RET(do_dup(get_current_process(), (int64_t)regs[9], 0, 0));
}

uint64_t sys_dup2(uint64_t *regs)
{
    RET(do_dup_to(get_current_process(), (int64_t)regs[9], (int64_t)regs[10], 0));
}

uint64_t sys_dup3(uint64_t *regs)
{
    int64_t o = (int64_t)regs[9], n = (int64_t)regs[10];
    uint64_t fl = regs[11];
    if (fl & ~(uint64_t)O_CLOEXEC)
        RET(-EINVAL);
    if (o == n)
        RET(-EINVAL);
    RET(do_dup_to(get_current_process(), o, n, (fl & O_CLOEXEC) != 0));
}

uint64_t sys_fcntl(uint64_t *regs)
{
    process_t *p = get_current_process();
    int64_t fd = (int64_t)regs[9];
    int cmd = (int)regs[10];
    uint64_t arg = regs[11];
    struct kfile *f = fd_get(p, fd);
    if (!f)
        RET(-EBADF);
    switch (cmd)
    {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
        if (arg >= MAX_FDS)
            RET(-EINVAL);
        RET(do_dup(p, fd, (int)arg, cmd == F_DUPFD_CLOEXEC));
    case F_GETFD:
        RET(p->fd_flags[fd] & FD_CLOEXEC);
    case F_SETFD:
        p->fd_flags[fd] = (arg & FD_CLOEXEC) ? FD_CLOEXEC : 0;
        RET(0);
    case F_GETFL:
        RET(f->flags);
    case F_SETFL:
        f->flags = (f->flags & ~(KO_APPEND | O_NONBLOCK)) |
                   (int)(arg & (KO_APPEND | O_NONBLOCK));
        RET(0);
    default:
        RET(-EINVAL);
    }
}

int kfd_poll(process_t *p, int64_t fd)
{
    struct kfile *f = fd_get(p, fd);
    if (!f)
        return 0x20;
    switch (f->type)
    {
    case FT_PIPE_R:
    case FT_PIPE_W:
        return pipe_poll(f);
    case FT_CONSOLE:
        if ((f->flags & KO_ACCMODE) == 1)
            return 4;
        return (tty_poll() ? 1 : 0) | 4;
    default:
        return 1 | 4;
    }
}
