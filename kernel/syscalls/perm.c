#include "syscalls.h"
#include "process.h"
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "Xjail/xv6/xv6fs_api.h"
#include "ext2/ext2.h"
#include "perm.h"
extern struct inode *nameiparent(char *path, char *name);
extern int namei_nofollow;
extern struct inode *namei(char *path);
extern struct inode *ext2_dirlookup(struct inode *dp, const char *name, int len);
extern int namei_eacces;
int ext2_new_uid, ext2_new_gid;
void perm_set_creator(process_t *p) { ext2_new_uid = (int)p->cred.euid; ext2_new_gid = (int)p->cred.egid; }
void perm_clear_creator(void) { ext2_new_uid = 0; ext2_new_gid = 0; }
static int in_group(const cred_t *c, uint32_t gid)
{
    if (c->egid == gid)
        return 1;
    for (int i = 0; i < c->ngroups; i++)
        if (c->groups[i] == gid)
            return 1;
    return 0;
}
static int check_bits(const cred_t *c, uint32_t uid, uint32_t gid, uint32_t mode, int want, int as_real)
{
    uint32_t id = as_real ? c->uid : c->euid;
    if (id == 0)
    {
        if (!(want & P_X) || (mode & 0170000) == 0040000 || (mode & 0111))
            return 0;
        return -EACCES;
    }
    uint32_t bits;
    if (id == uid)
        bits = (mode >> 6) & 7;
    else if (as_real ? (c->gid == gid || in_group(c, gid)) : in_group(c, gid))
        bits = (mode >> 3) & 7;
    else
        bits = mode & 7;
    return (bits & (uint32_t)want) == (uint32_t)want ? 0 : -EACCES;
}
int perm_inode_ids(uint32_t uid, uint32_t gid, process_t *p, struct inode *ip, int want, int locked)
{
    (void)uid; (void)gid;
    if (!p || !ip || ip->dev != EXT2_DEV)
        return 0;
    if (!locked)
        ilock(ip);
    uint32_t mode = ip->mode, ouid = ip->uid, ogid = ip->gid;
    if (!locked)
        iunlock(ip);
    return check_bits(&p->cred, ouid, ogid, mode, want, 0);
}
int perm_inode_l(process_t *p, struct inode *ip, int want) { return perm_inode_ids(0, 0, p, ip, want, 1); }
int perm_inode(process_t *p, struct inode *ip, int want) { return perm_inode_ids(0, 0, p, ip, want, 0); }
int perm_inode_real(process_t *p, struct inode *ip, int want)
{
    if (!p || !ip || ip->dev != EXT2_DEV)
        return 0;
    ilock(ip);
    uint32_t mode = ip->mode, ouid = ip->uid, ogid = ip->gid;
    iunlock(ip);
    return check_bits(&p->cred, ouid, ogid, mode, want, 1);
}
int perm_owner(process_t *p, struct inode *ip)
{
    if (!ip || ip->dev != EXT2_DEV || p->cred.euid == 0)
        return 0;
    ilock(ip);
    uint32_t o = ip->uid;
    iunlock(ip);
    return o == p->cred.euid ? 0 : -EPERM;
}
int perm_search_hook(struct inode *ip)
{
    process_t *p = get_current_process();
    if (!p || p->cred.euid == 0 || ip->dev != EXT2_DEV)
        return 0;
    return check_bits(&p->cred, ip->uid, ip->gid, ip->mode, P_X, 0) != 0;
}
static struct inode *parent_of(const char *path)
{
    char tmp[512], nm[16];
    int i = 0;
    for (; path[i] && i < 511; i++)
        tmp[i] = path[i];
    tmp[i] = 0;
    return nameiparent(tmp, nm);
}
int perm_parent(process_t *p, const char *path, int want)
{
    if (p->cred.euid == 0)
        return 0;
    struct inode *dp = parent_of(path);
    if (!dp)
        return 0;
    int r = perm_inode(p, dp, want);
    iput(dp);
    return r;
}
int perm_remove(process_t *p, const char *path)
{
    if (p->cred.euid == 0)
        return 0;
    struct inode *dp = parent_of(path);
    if (!dp)
        return 0;
    int r = perm_inode(p, dp, P_W | P_X);
    if (r == 0 && dp->dev == EXT2_DEV)
    {
        ilock(dp);
        uint32_t dm = dp->mode, du = dp->uid;
        iunlock(dp);
        if (dm & 01000)
        {
            namei_nofollow = 1;
            struct inode *c = namei((char *)path);
            namei_nofollow = 0;
            if (c)
            {
                ilock(c);
                uint32_t cu = c->uid;
                iunlock(c);
                iput(c);
                if (p->cred.euid != cu && p->cred.euid != du)
                    r = -EPERM;
            }
        }
    }
    iput(dp);
    return r;
}
int perm_can_kill(process_t *from, process_t *to)
{
    const cred_t *a = &from->cred, *b = &to->cred;
    if (a->euid == 0)
        return 0;
    if (a->uid == b->uid || a->uid == b->suid || a->euid == b->uid || a->euid == b->suid)
        return 0;
    return -EPERM;
}
int perm_in_group(process_t *p, uint32_t gid) { return in_group(&p->cred, gid); }
