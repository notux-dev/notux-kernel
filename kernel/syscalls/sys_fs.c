#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "syscall.h"
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "kfile.h"
#include "procfs.h"
#include "Xjail/xv6/xv6fs_api.h"
#include "ext2/ext2.h"
extern int namei_nofollow, namei_enotdir;
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif


extern struct inode *idup(struct inode *ip);
extern struct inode *dirlookup(struct inode *dp, char *name, uint *poff);
extern void xv6fs_bridge_use_cwd(void *ip);
extern struct inode *xv6fs_bridge_root(void);

#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)
#define PATH_MAX 512

#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFREG 0100000

struct kstat {
    uint64_t st_dev, st_ino, st_nlink;
    uint32_t st_mode, st_uid, st_gid, pad0;
    uint64_t st_rdev;
    int64_t  st_size, st_blksize, st_blocks;
    uint64_t at_s, at_ns, mt_s, mt_ns, ct_s, ct_ns;
    int64_t  unused[3];
};

static struct inode *fd_inode(process_t *p, int64_t fd)
{
    return fd_inode_of(p, fd);
}

static struct inode *namei_at(process_t *p, int64_t dirfd, char *path)
{
    if (path[0] == '/' || dirfd == AT_FDCWD)
        return namei(path);
    struct inode *d = fd_inode(p, dirfd);
    if (!d)
        return 0;
    xv6fs_bridge_use_cwd(d);
    struct inode *r = namei(path);
    xv6fs_bridge_use_cwd(p->cwd);
    return r;
}

static int strcmp_(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a != *b;
}
extern struct inode *iget(uint dev, uint inum);

static void fill_stat(struct kstat *st, struct inode *ip)
{
    uint8_t *z = (uint8_t *)st;
    for (unsigned i = 0; i < sizeof(*st); i++)
        z[i] = 0;
    st->st_dev = ip->dev;
    st->st_ino = ip->inum;
    st->st_nlink = ip->nlink;
    if (ip->dev == EXT2_DEV)
    {
        st->st_mode = ip->mode;
        st->st_uid = ip->uid;
        st->st_gid = ip->gid;
        st->at_s = ip->atime;
        st->mt_s = ip->mtime;
        st->ct_s = ip->ctime;
    }
    else
        st->st_mode = (ip->type == T_DIR ? S_IFDIR :
                   ip->type == T_DEV ? S_IFCHR : S_IFREG) | (ip->type == T_DEV ? 0666 : 0755);
    if (ip->type == T_DEV)
        st->st_rdev = ((uint64_t)ip->major << 8) | (uint64_t)(ip->minor & 0xff);
    st->st_size = ip->size;
    st->st_blksize = 4096;
    st->st_blocks = (ip->size + 511) / 512;
}

static uint64_t do_stat_inode(uint64_t *regs, struct inode *ip, uint64_t ubuf)
{
    struct kstat st;
    ilock(ip);
    fill_stat(&st, ip);
    iunlock(ip);
    if (copy_to_user(get_current_process(), ubuf, &st, sizeof(st)) < 0)
        RET(-EFAULT);
    RET(0);
}

static uint64_t stat_path(uint64_t *regs, int64_t dirfd, uint64_t upath,
                          uint64_t ubuf, int nofollow)
{
    process_t *p = get_current_process();
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, upath, PATH_MAX) < 0)
        RET(-EFAULT);
    if (!path[0])
        RET(-ENOENT);
    if (path[0] == '/')
    {
        uint32_t pm;
        uint64_t pino;
        int pr = proc_stat(p, path, &pm, &pino);
        if (pr != PROC_NOTMINE)
        {
            if (pr < 0)
                RET(pr);
            struct kstat st;
            uint8_t *z = (uint8_t *)&st;
            for (unsigned i = 0; i < sizeof(st); i++)
                z[i] = 0;
            st.st_ino = pino;
            st.st_nlink = (pm & S_IFDIR) == S_IFDIR ? 2 : 1;
            st.st_mode = pm;
            st.st_blksize = 4096;
            if (copy_to_user(p, ubuf, &st, sizeof(st)) < 0)
                RET(-EFAULT);
            RET(0);
        }
    }
    namei_nofollow = nofollow;
    struct inode *ip = namei_at(p, dirfd, path);
    namei_nofollow = 0;
    if (!ip)
        RET(namei_enotdir ? -ENOTDIR : -ENOENT);
    uint64_t r = do_stat_inode(regs, ip, ubuf);
    iput(ip);
    return r;
}

uint64_t sys_stat(uint64_t *regs)  { return stat_path(regs, AT_FDCWD, regs[9], regs[10], 0); }
uint64_t sys_lstat(uint64_t *regs) { return stat_path(regs, AT_FDCWD, regs[9], regs[10], 1); }
uint64_t sys_newfstatat(uint64_t *regs)
{
    process_t *p = get_current_process();
    int64_t dirfd = (int64_t)regs[9];
    uint64_t upath = regs[10], ubuf = regs[11], flags = regs[5];
    char c;
    if (copy_from_user(p, &c, upath, 1) < 0)
        RET(-EFAULT);
    if (!c && (flags & 0x1000))
    {
        struct inode *ip = fd_inode(p, dirfd);
        if (!ip)
            RET(-EBADF);
        return do_stat_inode(regs, ip, ubuf);
    }
    return stat_path(regs, dirfd, upath, ubuf, (flags & 0x100) != 0);
}

uint64_t sys_fstat(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct inode *ip = fd_inode(p, (int64_t)regs[9]);
    if (ip)
        return do_stat_inode(regs, ip, regs[10]);
    struct kfile *kf = fd_get(p, (int64_t)regs[9]);
    if (kf)
    {
        struct kstat st;
        uint8_t *z = (uint8_t *)&st;
        for (unsigned i = 0; i < sizeof(st); i++)
            z[i] = 0;
        st.st_mode = (kf->type == FT_CONSOLE) ? (S_IFCHR | 0620) : (0010000 | 0600);
        if (kf->type == FT_NULL || kf->type == FT_ZERO || kf->type == FT_RANDOM)
            st.st_mode = S_IFCHR | 0666;
        if (kf->type == FT_CONSOLE || kf->type == FT_NULL || kf->type == FT_ZERO || kf->type == FT_RANDOM)
            st.st_rdev = kf->pkind ? (uint64_t)kf->pkind : (5 << 8) | 1;
        if (kf->type == FT_PROC)
            st.st_mode = proc_f_is_dir(kf) ? (S_IFDIR | 0555) : (S_IFREG | 0444);
        st.st_blksize = 1024;
        st.st_nlink = 1;
        if (copy_to_user(p, regs[10], &st, sizeof(st)) < 0)
            RET(-EFAULT);
        RET(0);
    }
    RET(-EBADF);
}

uint64_t sys_access(uint64_t *regs)
{
    process_t *p = get_current_process();
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, regs[9], PATH_MAX) < 0)
        RET(-EFAULT);
    {
        uint32_t pm; uint64_t pino;
        int pr = proc_stat(p, path, &pm, &pino);
        if (pr != PROC_NOTMINE)
            RET(pr < 0 ? pr : 0);
    }
    struct inode *ip = namei(path);
    if (!ip)
        RET(-ENOENT);
    iput(ip);
    RET(0);
}
uint64_t sys_faccessat(uint64_t *regs)
{
    process_t *p = get_current_process();
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, regs[10], PATH_MAX) < 0)
        RET(-EFAULT);
    if (path[0] == '/')
    {
        uint32_t pm; uint64_t pino;
        int pr = proc_stat(p, path, &pm, &pino);
        if (pr != PROC_NOTMINE)
            RET(pr < 0 ? pr : 0);
    }
    struct inode *ip = namei_at(p, (int64_t)regs[9], path);
    if (!ip)
        RET(-ENOENT);
    iput(ip);
    RET(0);
}

uint64_t sys_chdir(uint64_t *regs)
{
    process_t *p = get_current_process();
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, regs[9], PATH_MAX) < 0)
        RET(-EFAULT);
    struct inode *ip = namei(path);
    if (!ip)
        RET(-ENOENT);
    ilock(ip);
    if (ip->type != T_DIR)
    {
        iunlock(ip); iput(ip);
        RET(-ENOTDIR);
    }
    iunlock(ip);
    if (p->cwd)
        iput((struct inode *)p->cwd);
    p->cwd = ip;
    xv6fs_bridge_use_cwd(ip);
    RET(0);
}

uint64_t sys_getcwd(uint64_t *regs)
{
    process_t *p = get_current_process();
    uint64_t ubuf = regs[9], size = regs[10];
    char tmp[PATH_MAX];
    int pos = PATH_MAX - 1;
    tmp[pos] = 0;
    struct inode *cur = p->cwd ? idup((struct inode *)p->cwd)
                               : idup(xv6fs_bridge_root());
    for (int depth = 0; depth < 32; depth++)
    {
        if (cur->dev == EXT2_DEV && cur->inum == EXT2_ROOT_INO && ext2_mountpoint())
        {
            struct inode *mp = iget(0, (uint)ext2_mountpoint());
            iput(cur);
            cur = mp;
        }
        else if (cur->dev == EXT2_DEV && ext2_bind_ino() &&
                 cur->inum == ext2_bind_ino())
        {
            struct inode *mp = iget(0, ext2_bind_mp());
            iput(cur);
            cur = mp;
        }
        ilock(cur);
        struct inode *par = dirlookup(cur, "..", 0);
        iunlock(cur);
        if (!par)
            break;
        if (par->inum == cur->inum)
        {
            iput(par);
            break;
        }
        char name[256];
        int found = 0;
        ilock(par);
        if (par->dev == EXT2_DEV)
        {
            char nm[256]; int nl, dt; uint ino, cp = 0;
            while (ext2_readdir(par, &cp, nm, &nl, &ino, &dt))
                if (ino == cur->inum && strcmp_(nm, ".") && strcmp_(nm, ".."))
                {
                    for (int i = 0; i <= nl; i++) name[i] = nm[i];
                    found = 1;
                    break;
                }
        }
        else
        for (uint off = 0; ; off += sizeof(struct dirent))
        {
            struct dirent de;
            if (readi(par, (char *)&de, off, sizeof(de)) != sizeof(de))
                break;
            if (de.inum == cur->inum && de.name[0] &&
                !(de.name[0] == '.' && (!de.name[1] ||
                  (de.name[1] == '.' && !de.name[2]))))
            {
                int n = 0;
                while (n < 14 && de.name[n]) { name[n] = de.name[n]; n++; }
                name[n] = 0;
                found = 1;
                break;
            }
        }
        iunlock(par);
        if (!found)
        {
            iput(par);
            break;
        }
        int n = 0;
        while (name[n]) n++;
        if (pos - n - 1 < 1)
        {
            iput(par); iput(cur);
            RET(-ERANGE);
        }
        pos -= n;
        for (int i = 0; i < n; i++) tmp[pos + i] = name[i];
        tmp[--pos] = '/';
        iput(cur);
        cur = par;
    }
    iput(cur);
    if (tmp[pos] == 0)
        tmp[--pos] = '/';
    uint64_t len = (uint64_t)(PATH_MAX - pos);
    if (len > size)
        RET(-ERANGE);
    if (copy_to_user(p, ubuf, &tmp[pos], len) < 0)
        RET(-EFAULT);
    RET(len);
}

uint64_t sys_getdents64(uint64_t *regs)
{
    process_t *p = get_current_process();
    int64_t fd = (int64_t)regs[9];
    uint64_t ubuf = regs[10], count = regs[11];
    struct inode *ip = fd_inode(p, fd);
    if (!ip)
    {
        struct kfile *kf = fd_get(p, fd);
        if (kf && kf->type == FT_PROC)
            RET(proc_getdents(p, kf, ubuf, count));
        RET(kf ? -ENOTDIR : -EBADF);
    }
    ilock(ip);
    if (ip->type != T_DIR)
    {
        iunlock(ip);
        RET(-ENOTDIR);
    }
    uint32_t pos = (uint32_t)*fd_offset(p, fd);
    uint64_t written = 0;
    if (ip->dev == EXT2_DEV)
    {
        for (;;)
        {
            char nm[256]; int nl, dt; uint ino, next = pos;
            if (!ext2_readdir(ip, &next, nm, &nl, &ino, &dt))
                break;
            uint16_t reclen = (uint16_t)((19 + nl + 1 + 7) & ~7);
            if (written + reclen > count)
            {
                if (!written) { iunlock(ip); RET(-EINVAL); }
                break;
            }
            uint8_t rec[19 + 256 + 8];
            for (int i = 0; i < (int)sizeof(rec); i++) rec[i] = 0;
            *(uint64_t *)(rec + 0) = ino;
            *(uint64_t *)(rec + 8) = next;
            *(uint16_t *)(rec + 16) = reclen;
            rec[18] = (uint8_t)dt;
            for (int i = 0; i < nl; i++) rec[19 + i] = (uint8_t)nm[i];
            if (copy_to_user(p, ubuf + written, rec, reclen) < 0)
            {
                iunlock(ip);
                RET(written ? (int64_t)written : -EFAULT);
            }
            written += reclen;
            pos = next;
        }
        iunlock(ip);
        *fd_offset(p, fd) = pos;
        RET(written);
    }
    for (;;)
    {
        struct dirent de;
        if (readi(ip, (char *)&de, pos, sizeof(de)) != sizeof(de))
            break;
        if (de.inum == 0)
        {
            pos += sizeof(de);
            continue;
        }
        int n = 0;
        while (n < 14 && de.name[n]) n++;
        uint16_t reclen = (uint16_t)((19 + n + 1 + 7) & ~7);
        if (written + reclen > count)
        {
            if (!written)
            {
                iunlock(ip);
                RET(-EINVAL);
            }
            break;
        }
        uint8_t rec[48];
        for (int i = 0; i < 48; i++) rec[i] = 0;
        *(uint64_t *)(rec + 0) = de.inum;
        *(uint64_t *)(rec + 8) = pos + sizeof(de);
        *(uint16_t *)(rec + 16) = reclen;
        rec[18] = DT_UNKNOWN;
        for (int i = 0; i < n; i++) rec[19 + i] = (uint8_t)de.name[i];
        if (copy_to_user(p, ubuf + written, rec, reclen) < 0)
        {
            iunlock(ip);
            RET(written ? (int64_t)written : -EFAULT);
        }
        written += reclen;
        pos += sizeof(de);
    }
    iunlock(ip);
    *fd_offset(p, fd) = pos;
    RET(written);
}

uint64_t sys_pread64(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct inode *ip = fd_inode(p, (int64_t)regs[9]);
    uint64_t ubuf = regs[10], count = regs[11], off = regs[5];
    if (!ip)
        RET(fd_get(p, (int64_t)regs[9]) ? -ESPIPE : -EBADF);
    ilock(ip);
    if (ip->type == T_DIR)
    {
        iunlock(ip);
        RET(-EISDIR);
    }
    uint64_t done = 0;
    char buf[512];
    while (done < count)
    {
        uint64_t n = count - done;
        if (n > sizeof(buf))
            n = sizeof(buf);
        int r = readi(ip, buf, (uint)(off + done), (uint)n);
        if (r <= 0)
            break;
        if (copy_to_user(p, ubuf + done, buf, (uint64_t)r) < 0)
        {
            iunlock(ip);
            RET(done ? (int64_t)done : -EFAULT);
        }
        done += (uint64_t)r;
        if ((uint64_t)r < n)
            break;
    }
    iunlock(ip);
    RET(done);
}

static uint64_t do_readlink(uint64_t *regs, int64_t dirfd, uint64_t upath, uint64_t ubuf, uint64_t bufsiz)
{
    process_t *p = get_current_process();
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, upath, PATH_MAX) < 0)
        RET(-EFAULT);
    if (!path[0])
        RET(-ENOENT);
    namei_nofollow = 1;
    struct inode *ip = namei_at(p, dirfd, path);
    namei_nofollow = 0;
    if (!ip)
        RET(-ENOENT);
    ilock(ip);
    if (ip->dev != EXT2_DEV || ip->type != T_LNK)
    {
        iunlock(ip); iput(ip);
        RET(-EINVAL);
    }
    char buf[512];
    int n = ext2_readlink(ip, buf, bufsiz < sizeof(buf) ? (int)bufsiz : (int)sizeof(buf));
    iunlock(ip); iput(ip);
    if (n < 0)
        RET(-EIO);
    if (copy_to_user(p, ubuf, buf, (uint64_t)n) < 0)
        RET(-EFAULT);
    RET(n);
}
uint64_t sys_readlink(uint64_t *regs)   { return do_readlink(regs, AT_FDCWD, regs[9], regs[10], regs[11]); }
uint64_t sys_readlinkat(uint64_t *regs) { return do_readlink(regs, (int64_t)regs[9], regs[10], regs[11], regs[5]); }

uint64_t sys_umask(uint64_t *regs) { RET(022); }

uint64_t sys_uname(uint64_t *regs)
{
    char u[6 * 65];
    for (int i = 0; i < (int)sizeof(u); i++) u[i] = 0;
    const char *f[6] = {"notux", "notux", "6.1.0", "notux", "x86_64", "(none)"};
    for (int k = 0; k < 6; k++)
        for (int i = 0; f[k][i]; i++) u[k * 65 + i] = f[k][i];
    if (copy_to_user(get_current_process(), regs[9], u, sizeof(u)) < 0)
        RET(-EFAULT);
    RET(0);
}

uint64_t sys_getpgrp(uint64_t *regs) { RET(get_current_process()->pgid); }
uint64_t sys_getpgid(uint64_t *regs)
{
    process_t *t = regs[9] ? get_process_by_pid((uint32_t)regs[9]) : get_current_process();
    RET(t ? (int64_t)t->pgid : -ESRCH);
}
uint64_t sys_getsid(uint64_t *regs)
{
    process_t *t = regs[9] ? get_process_by_pid((uint32_t)regs[9]) : get_current_process();
    RET(t ? (int64_t)t->sid : -ESRCH);
}
uint64_t sys_setpgid(uint64_t *regs)
{
    process_t *me = get_current_process();
    process_t *t = regs[9] ? get_process_by_pid((uint32_t)regs[9]) : me;
    int64_t pg = (int64_t)regs[10];
    if (!t)
        RET(-ESRCH);
    if (t != me && !(t->ppid == me->pid && t->sid == me->sid))
        RET(-ESRCH);
    if (t->sid == t->pid)
        RET(-EPERM);
    if (pg < 0)
        RET(-EINVAL);
    if (pg == 0)
        pg = t->pid;
    if ((uint32_t)pg != t->pid)
    {
        int ok = 0;
        for (int i = 0; i < MAX_PROCESSES; i++)
            if (processes[i].state != PROCESS_STATE_TERMINATED && processes[i].pgid == (uint32_t)pg &&
                processes[i].sid == t->sid)
                ok = 1;
        if (!ok)
            RET(-EPERM);
    }
    t->pgid = (uint32_t)pg;
    RET(0);
}
uint64_t sys_setsid(uint64_t *regs)
{
    process_t *me = get_current_process();
    if (me->pgid == me->pid)
        RET(-EPERM);
    me->sid = me->pid;
    me->pgid = me->pid;
    RET(me->pid);
}
