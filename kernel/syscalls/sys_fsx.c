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
#include "Xjail/xv6/xv6fs_api.h"
#include "ext2/ext2.h"
#include "ktime.h"


extern void xv6fs_bridge_use_cwd(void *ip);
extern struct inode *idup(struct inode *ip);

#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)
#define PATH_MAX 512

static int get_path(process_t *p, int64_t dirfd, uint64_t upath, char *path)
{
    if (strncpy_from_user(p, path, upath, PATH_MAX) < 0)
        return -EFAULT;
    if (path[0] == 0)
        return -ENOENT;
    if (path[0] != '/' && dirfd != AT_FDCWD)
    {
        struct inode *d = fd_inode_of(p, dirfd);
        if (!d)
            return fd_get(p, dirfd) ? -ENOTDIR : -EBADF;
        xv6fs_bridge_use_cwd(d);
    }
    return 0;
}

static void done_path(process_t *p)
{
    xv6fs_bridge_use_cwd(p->cwd);
}


static int64_t do_mkdir(process_t *p, int64_t dirfd, uint64_t upath, uint64_t mode)
{
    char path[PATH_MAX];
    int e = get_path(p, dirfd, upath, path);
    if (e)
        return e;
    int64_t r = 0;
    if (path_last_too_long(path))
        r = -ENAMETOOLONG;
    else
    {
        struct inode *x = namei(path);
        if (x)
        {
            iput(x);
            r = -EEXIST;
        }
        else
        {
            extern int xv6fs_create_mode, xv6fs_errno;
            xv6fs_create_mode = (int)(mode & ~022u);
            struct inode *ip = xv6fs_create(path, T_DIR, 0, 0);
            xv6fs_create_mode = -1;
            if (!ip)
                r = xv6fs_errno ? xv6fs_errno : -ENOENT;
            else
            {
                iunlock(ip);
                iput(ip);
            }
        }
    }
    done_path(p);
    return r;
}

uint64_t sys_mkdir(uint64_t *regs)   { RET(do_mkdir(get_current_process(), AT_FDCWD, regs[9], regs[10])); }
uint64_t sys_mkdirat(uint64_t *regs) { RET(do_mkdir(get_current_process(), (int64_t)regs[9], regs[10], regs[11])); }

static int64_t do_remove(process_t *p, int64_t dirfd, uint64_t upath, int want)
{
    char path[PATH_MAX];
    int e = get_path(p, dirfd, upath, path);
    if (e)
        return e;
    int64_t r = xv6fs_remove(path, want);
    done_path(p);
    return r;
}

uint64_t sys_unlink(uint64_t *regs) { RET(do_remove(get_current_process(), AT_FDCWD, regs[9], 0)); }
uint64_t sys_rmdir(uint64_t *regs)  { RET(do_remove(get_current_process(), AT_FDCWD, regs[9], 1)); }
uint64_t sys_unlinkat(uint64_t *regs)
{
    RET(do_remove(get_current_process(), (int64_t)regs[9], regs[10],
                  (regs[11] & AT_REMOVEDIR) ? 1 : 0));
}


static int64_t do_two(process_t *p, int is_rename, int64_t d1, uint64_t u1, int64_t d2, uint64_t u2)
{
    char a[PATH_MAX], b[PATH_MAX];
    if (strncpy_from_user(p, a, u1, PATH_MAX) < 0 || strncpy_from_user(p, b, u2, PATH_MAX) < 0)
        return -EFAULT;
    if (!a[0] || !b[0])
        return -ENOENT;
    int64_t base = AT_FDCWD;
    if (a[0] != '/' && d1 != AT_FDCWD)
        base = d1;
    if (b[0] != '/' && d2 != AT_FDCWD)
    {
        if (base != AT_FDCWD && base != d2)
            return -EINVAL;
        base = d2;
    }
    if (base != AT_FDCWD)
    {
        struct inode *d = fd_inode_of(p, base);
        if (!d)
            return fd_get(p, base) ? -ENOTDIR : -EBADF;
        xv6fs_bridge_use_cwd(d);
    }
    int64_t r;
    if (path_last_too_long(b))
        r = -ENAMETOOLONG;
    else
        r = is_rename ? xv6fs_rename(a, b) : xv6fs_link(a, b);
    done_path(p);
    return r;
}

uint64_t sys_rename(uint64_t *regs)
{
    RET(do_two(get_current_process(), 1, AT_FDCWD, regs[9], AT_FDCWD, regs[10]));
}
uint64_t sys_renameat(uint64_t *regs)
{
    RET(do_two(get_current_process(), 1, (int64_t)regs[9], regs[10], (int64_t)regs[11], regs[5]));
}
uint64_t sys_renameat2(uint64_t *regs)
{
    if (regs[7])
        RET(-EINVAL);
    RET(do_two(get_current_process(), 1, (int64_t)regs[9], regs[10], (int64_t)regs[11], regs[5]));
}
uint64_t sys_link(uint64_t *regs)
{
    RET(do_two(get_current_process(), 0, AT_FDCWD, regs[9], AT_FDCWD, regs[10]));
}
uint64_t sys_linkat(uint64_t *regs)
{
    RET(do_two(get_current_process(), 0, (int64_t)regs[9], regs[10], (int64_t)regs[11], regs[5]));
}

uint64_t sys_eperm(uint64_t *regs)    { RET(-EPERM); }
uint64_t sys_eopnotsupp(uint64_t *regs) { RET(-EOPNOTSUPP); }


static int64_t do_ftruncate_inode(struct inode *ip, int64_t len)
{
    if (len < 0)
        return -EINVAL;
    ilock(ip);
    if (ip->type == T_DIR)
    {
        iunlock(ip);
        return -EISDIR;
    }
    int r = xv6fs_truncate(ip, (uint)len);
    iunlock(ip);
    return r;
}

uint64_t sys_ftruncate(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct kfile *f = fd_get(p, (int64_t)regs[9]);
    if (!f)
        RET(-EBADF);
    if (f->type != FT_INODE || (f->flags & KO_ACCMODE) == 0)
        RET(-EINVAL);
    RET(do_ftruncate_inode(f->ip, (int64_t)regs[10]));
}

uint64_t sys_truncate(uint64_t *regs)
{
    process_t *p = get_current_process();
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, regs[9], PATH_MAX) < 0)
        RET(-EFAULT);
    struct inode *ip = namei(path);
    if (!ip)
        RET(-ENOENT);
    int64_t r = do_ftruncate_inode(ip, (int64_t)regs[10]);
    iput(ip);
    RET(r);
}

uint64_t sys_pwrite64(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct kfile *f = fd_get(p, (int64_t)regs[9]);
    uint64_t ubuf = regs[10], count = regs[11], off = regs[5];
    if (!f)
        RET(-EBADF);
    if (f->type != FT_INODE)
        RET(-ESPIPE);
    if ((f->flags & KO_ACCMODE) == 0)
        RET(-EBADF);
    struct inode *ip = f->ip;
    char buf[512];
    uint64_t done = 0;
    ilock(ip);
    if (ip->type == T_DIR)
    {
        iunlock(ip);
        RET(-EISDIR);
    }
    while (done < count)
    {
        uint64_t n = count - done;
        if (n > sizeof buf)
            n = sizeof buf;
        if (copy_from_user(p, buf, ubuf + done, n) < 0)
        {
            iunlock(ip);
            RET(done ? (int64_t)done : -EFAULT);
        }
        int r = writei(ip, buf, (uint)(off + done), (uint)n);
        if (r <= 0)
            break;
        done += r;
        if ((uint64_t)r < n)
            break;
    }
    iupdate(ip);
    iunlock(ip);
    RET(done ? (int64_t)done : (count ? -ENOSPC : 0));
}


struct kstatfs {
    int64_t f_type, f_bsize;
    uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t f_fsid[2];
    int64_t f_namelen, f_frsize, f_flags, f_spare[4];
};

static int64_t fill_statfs(process_t *p, uint64_t ubuf, struct inode *ip)
{
    uint blocks, bfree, files, ffree, bsize = BSIZE;
    int is_ext2 = ip && ip->dev == EXT2_DEV;
    if (is_ext2)
        ext2_statfs(&blocks, &bfree, &files, &ffree, &bsize);
    else
        xv6fs_statfs(&blocks, &bfree, &files, &ffree);
    struct kstatfs s;
    uint8_t *z = (uint8_t *)&s;
    for (unsigned i = 0; i < sizeof s; i++)
        z[i] = 0;
    s.f_type = is_ext2 ? 0xEF53 : 0x58365846;
    s.f_bsize = bsize;
    s.f_blocks = blocks;
    s.f_bfree = s.f_bavail = bfree;
    s.f_files = files;
    s.f_ffree = ffree;
    s.f_namelen = is_ext2 ? 255 : 14;
    s.f_frsize = bsize;
    return copy_to_user(p, ubuf, &s, sizeof s) < 0 ? -EFAULT : 0;
}

uint64_t sys_statfs(uint64_t *regs)
{
    process_t *p = get_current_process();
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, regs[9], PATH_MAX) < 0)
        RET(-EFAULT);
    struct inode *ip = namei(path);
    if (!ip)
        RET(-ENOENT);
    int64_t sr = fill_statfs(p, regs[10], ip);
    iput(ip);
    RET(sr);
}

uint64_t sys_fstatfs(uint64_t *regs)
{
    process_t *p = get_current_process();
    if (!fd_get(p, (int64_t)regs[9]))
        RET(-EBADF);
    RET(fill_statfs(p, regs[10], fd_inode_of(p, (int64_t)regs[9])));
}


uint64_t sys_fchdir(uint64_t *regs)
{
    process_t *p = get_current_process();
    struct inode *ip = fd_inode_of(p, (int64_t)regs[9]);
    if (!ip)
        RET(fd_get(p, (int64_t)regs[9]) ? -ENOTDIR : -EBADF);
    ilock(ip);
    int isdir = ip->type == T_DIR;
    iunlock(ip);
    if (!isdir)
        RET(-ENOTDIR);
    idup(ip);
    if (p->cwd)
        iput((struct inode *)p->cwd);
    p->cwd = ip;
    xv6fs_bridge_use_cwd(ip);
    RET(0);
}

uint64_t sys_fsync(uint64_t *regs)
{
    if (!fd_get(get_current_process(), (int64_t)regs[9]))
        RET(-EBADF);
    RET(0);
}
uint64_t sys_sync(uint64_t *regs) { RET(0); }


uint64_t sys_path_stub(uint64_t *regs)
{
    process_t *p = get_current_process();
    char path[PATH_MAX];
    if (strncpy_from_user(p, path, regs[9], PATH_MAX) < 0)
        RET(-EFAULT);
    struct inode *ip = namei(path);
    if (!ip)
        RET(-ENOENT);
    iput(ip);
    RET(0);
}

uint64_t sys_pathat_stub(uint64_t *regs)
{
    process_t *p = get_current_process();
    if (regs[10] == 0)
        RET(fd_get(p, (int64_t)regs[9]) ? 0 : -EBADF);
    char path[PATH_MAX];
    int e = get_path(p, (int64_t)regs[9], regs[10], path);
    if (e)
        RET(e);
    struct inode *ip = namei(path);
    done_path(p);
    if (!ip)
        RET(-ENOENT);
    iput(ip);
    RET(0);
}

uint64_t sys_fd_stub(uint64_t *regs)
{
    if (!fd_get(get_current_process(), (int64_t)regs[9]))
        RET(-EBADF);
    RET(0);
}


extern int namei_nofollow, namei_enotdir;
extern char namei_lastname[256];
extern int  namei_lastlen;
struct inode *nameiparent(char *path, char *name);

static int apply_attr(struct inode *ip, int64_t mode, int64_t uid, int64_t gid, int64_t at, int64_t mt)
{
    ilock(ip);
    int r = 0;
    if (ip->dev == EXT2_DEV)
        r = ext2_setattr(ip, mode, uid, gid, at, mt);
    iunlock(ip);
    return r;
}

static int64_t attr_path(process_t *p, int64_t dirfd, uint64_t upath, int nofollow,
                         int64_t mode, int64_t uid, int64_t gid, int64_t at, int64_t mt)
{
    char path[PATH_MAX];
    int e = get_path(p, dirfd, upath, path);
    if (e)
        return e;
    namei_nofollow = nofollow;
    struct inode *ip = namei(path);
    namei_nofollow = 0;
    done_path(p);
    if (!ip)
        return namei_enotdir ? -ENOTDIR : -ENOENT;
    int r = apply_attr(ip, mode, uid, gid, at, mt);
    iput(ip);
    return r;
}

static int64_t attr_fd(process_t *p, int64_t fd, int64_t mode, int64_t uid, int64_t gid, int64_t at, int64_t mt)
{
    struct inode *ip = fd_inode_of(p, fd);
    if (!ip)
        return fd_get(p, fd) ? 0 : -EBADF;
    return apply_attr(ip, mode, uid, gid, at, mt);
}

static int64_t id_arg(uint64_t v) { return (uint32_t)v == 0xFFFFFFFFu ? -1 : (int64_t)(uint32_t)v; }
static int64_t now_s(void) { return (int64_t)(ktime_realtime_ns() / 1000000000ull); }

uint64_t sys_chmod(uint64_t *regs)    { RET(attr_path(get_current_process(), AT_FDCWD, regs[9], 0, (int64_t)(regs[10] & 07777), -1, -1, -1, -1)); }
uint64_t sys_fchmod(uint64_t *regs)   { RET(attr_fd(get_current_process(), (int64_t)regs[9], (int64_t)(regs[10] & 07777), -1, -1, -1, -1)); }
uint64_t sys_fchmodat(uint64_t *regs) { RET(attr_path(get_current_process(), (int64_t)regs[9], regs[10], 0, (int64_t)(regs[11] & 07777), -1, -1, -1, -1)); }
uint64_t sys_chown(uint64_t *regs)    { RET(attr_path(get_current_process(), AT_FDCWD, regs[9], 0, -1, id_arg(regs[10]), id_arg(regs[11]), -1, -1)); }
uint64_t sys_lchown(uint64_t *regs)   { RET(attr_path(get_current_process(), AT_FDCWD, regs[9], 1, -1, id_arg(regs[10]), id_arg(regs[11]), -1, -1)); }
uint64_t sys_fchown(uint64_t *regs)   { RET(attr_fd(get_current_process(), (int64_t)regs[9], -1, id_arg(regs[10]), id_arg(regs[11]), -1, -1)); }
uint64_t sys_fchownat(uint64_t *regs)
{
    RET(attr_path(get_current_process(), (int64_t)regs[9], regs[10], (regs[7] & 0x100) != 0, -1,
                  id_arg(regs[11]), id_arg(regs[5]), -1, -1));
}

static int64_t do_utimes(process_t *p, int64_t dirfd, uint64_t upath, uint64_t utv)
{
    int64_t at = now_s(), mt = at;
    if (utv)
    {
        uint64_t tv[4];
        if (copy_from_user(p, tv, utv, sizeof tv) < 0)
            return -EFAULT;
        at = (int64_t)tv[0]; mt = (int64_t)tv[2];
    }
    return attr_path(p, dirfd, upath, 0, -1, -1, -1, at, mt);
}
uint64_t sys_utimes(uint64_t *regs)    { RET(do_utimes(get_current_process(), AT_FDCWD, regs[9], regs[10])); }
uint64_t sys_futimesat(uint64_t *regs) { RET(do_utimes(get_current_process(), (int64_t)regs[9], regs[10], regs[11])); }

uint64_t sys_utimensat(uint64_t *regs)
{
    process_t *p = get_current_process();
    int64_t at = now_s(), mt = at;
    uint64_t uts = regs[11];
    if (uts)
    {
        uint64_t ts[4];
        if (copy_from_user(p, ts, uts, sizeof ts) < 0)
            RET(-EFAULT);
        at = ts[1] == 0x3ffffffe ? -1 : ts[1] == 0x3fffffff ? now_s() : (int64_t)ts[0];
        mt = ts[3] == 0x3ffffffe ? -1 : ts[3] == 0x3fffffff ? now_s() : (int64_t)ts[2];
    }
    if (regs[10] == 0)
        RET(attr_fd(p, (int64_t)regs[9], -1, -1, -1, at, mt));
    RET(attr_path(p, (int64_t)regs[9], regs[10], (regs[5] & 0x100) != 0, -1, -1, -1, at, mt));
}

static int64_t do_symlink(process_t *p, uint64_t utarget, int64_t dirfd, uint64_t ulink)
{
    char target[512], path[PATH_MAX], name[16];
    int tl = strncpy_from_user(p, target, utarget, sizeof target);
    if (tl < 0)
        return -EFAULT;
    if (tl == 0)
        return -ENOENT;
    int e = get_path(p, dirfd, ulink, path);
    if (e)
        return e;
    int64_t r;
    struct inode *dp = nameiparent(path, name);
    if (!dp)
        r = -ENOENT;
    else if (dp->dev != EXT2_DEV)
    {
        iput(dp);
        r = -EPERM;
    }
    else
    {
        char nm[256];
        int len = namei_lastlen;
        for (int i = 0; i <= len; i++) nm[i] = namei_lastname[i];
        ilock(dp);
        r = ext2_symlink(dp, nm, len, target, tl);
        iunlock(dp);
        iput(dp);
    }
    done_path(p);
    return r;
}
uint64_t sys_symlink(uint64_t *regs)   { RET(do_symlink(get_current_process(), regs[9], AT_FDCWD, regs[10])); }
uint64_t sys_symlinkat(uint64_t *regs) { RET(do_symlink(get_current_process(), regs[9], (int64_t)regs[10], regs[11])); }
