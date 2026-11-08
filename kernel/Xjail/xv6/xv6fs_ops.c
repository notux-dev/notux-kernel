#include "types.h"   
#include "fs.h"      
#include "file.h"    
#include "defs.h"    
#include "xv6fs_api.h"
#include "../../ext2/ext2.h"
struct inode* nameiparent(char *path, char *name);
struct inode* dirlookup(struct inode *dp, char *name, uint *off);
int dirlink(struct inode *dp, char *name, uint inum);
struct inode* ialloc(uint dev, short type);
int namecmp(const char *s, const char *t);
extern char namei_lastname[256];
extern int  namei_lastlen;
int xv6fs_create_mode = -1;
int xv6fs_errno;
struct inode* xv6fs_create(char *path, short type, short major, short minor) {
    struct inode *ip, *dp;
    char name[DIRSIZ];
    xv6fs_errno = 0;
    if ((dp = nameiparent(path, name)) == 0)
        return 0;
    if (dp->dev == EXT2_DEV) {
        int len = namei_lastlen, r;
        char nm[256];
        if (len > 255) {
            iput(dp);
            xv6fs_errno = -36;
            return 0;
        }
        for (int i = 0; i <= len; i++) nm[i] = namei_lastname[i];
        ilock(dp);
        if ((ip = ext2_dirlookup(dp, nm, len)) != 0) {
            iunlock(dp);
            iput(dp);
            ilock(ip);
            if (type == T_FILE && ip->type == T_FILE)
                return ip;
            iunlock(ip);
            iput(ip);
            xv6fs_errno = -17;
            return 0;
        }
        uint mode = xv6fs_create_mode >= 0 ? (uint)xv6fs_create_mode & 07777 : (type == T_DIR ? 0755 : 0644);
        ip = 0;
        if (type == T_DIR) r = ext2_mkdir(dp, nm, len, mode, &ip);
        else if (type == T_FILE) r = ext2_create(dp, nm, len, mode, &ip);
        else r = -1;
        iunlock(dp);
        iput(dp);
        xv6fs_create_mode = -1;
        if (r < 0) { xv6fs_errno = r; return 0; }
        ilock(ip);
        return ip;
    }
    ilock(dp);
    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iunlock(dp);
        iput(dp);
        ilock(ip);
        if (type == T_FILE && ip->type == T_FILE) {
            return ip;
        }
        iunlock(ip);
        iput(ip);
        return 0;
    }
    if ((ip = ialloc(dp->dev, type)) == 0) {
        iunlock(dp);
        iput(dp);
        return 0;
    }
    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);
    if (type == T_DIR) {
        dp->nlink++;
        iupdate(dp);
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0) {
            iunlock(ip);
            iput(ip);
            iunlock(dp);
            iput(dp);
            return 0;
        }
    }
    if (dirlink(dp, name, ip->inum) < 0) {
        iunlock(ip);
        iput(ip);
        iunlock(dp);
        iput(dp);
        return 0;
    }
    iunlock(dp);
    iput(dp);
    return ip;
}
int xv6fs_remove(char *path, int want) {
    struct inode *ip, *dp;
    struct dirent de;
    char name[DIRSIZ];
    uint off;
    if ((dp = nameiparent(path, name)) == 0)
        return -2;
    ilock(dp);
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
        iunlock(dp);
        iput(dp);
        return want == 1 ? (name[1] == 0 ? -22 : -39) : -21;
    }
    if (dp->dev == EXT2_DEV) {
        int r = ext2_unlink(dp, namei_lastname, namei_lastlen, want);
        iunlock(dp);
        iput(dp);
        return r;
    }
    if ((ip = dirlookup(dp, name, &off)) == 0) {
        iunlock(dp);
        iput(dp);
        return -2;
    }
    ilock(ip);
    if (ip->nlink < 1) {
        iunlock(ip); iput(ip); iunlock(dp); iput(dp);
        return -2;
    }
    if (want == 0 && ip->type == T_DIR) {
        iunlock(ip); iput(ip); iunlock(dp); iput(dp);
        return -21;
    }
    if (want == 1 && ip->type != T_DIR) {
        iunlock(ip); iput(ip); iunlock(dp); iput(dp);
        return -20;
    }
    if (ip->type == T_DIR) {
        int empty = 1;
        uint o;
        for (o = 2 * sizeof(de); o < ip->size; o += sizeof(de)) {
            if (readi(ip, (char*)&de, o, sizeof(de)) != sizeof(de) || de.inum != 0) {
                empty = 0;
                break;
            }
        }
        if (!empty) {
            iunlock(ip); iput(ip); iunlock(dp); iput(dp);
            return -39;
        }
    }
    memset(&de, 0, sizeof(de));
    if (writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) {
        iunlock(ip); iput(ip); iunlock(dp); iput(dp);
        return -5;
    }
    if (ip->type == T_DIR) {
        dp->nlink--;
        iupdate(dp);
    }
    iunlock(dp);
    iput(dp);
    ip->nlink--;
    iupdate(ip);
    iunlock(ip);
    iput(ip);
    return 0;
}
int xv6fs_unlink(char *path) {
    return xv6fs_remove(path, -1) < 0 ? -1 : 0;
}

int xv6fs_link(char *oldp, char *newp) {
    struct inode *ip, *dp, *e;
    char name[DIRSIZ];
    int err;
    if ((ip = namei(oldp)) == 0)
        return -2;
    if (ip->dev == EXT2_DEV) {
        int r;
        if ((dp = nameiparent(newp, name)) == 0) { iput(ip); return -2; }
        if (dp->dev != EXT2_DEV) { iput(dp); iput(ip); return -18; }
        ilock(dp);
        ilock(ip);
        r = ext2_link(dp, namei_lastname, namei_lastlen, ip);
        iunlock(ip);
        iunlock(dp);
        iput(dp);
        iput(ip);
        return r;
    }
    ilock(ip);
    if (ip->type == T_DIR) {
        iunlock(ip); iput(ip);
        return -1;
    }
    ip->nlink++;
    iupdate(ip);
    iunlock(ip);
    if ((dp = nameiparent(newp, name)) == 0) {
        err = -2;
        goto bad;
    }
    ilock(dp);
    if (dp->dev != ip->dev) {
        err = -18;
        goto baddp;
    }
    if ((e = dirlookup(dp, name, 0)) != 0) {
        iput(e);
        err = -17;
        goto baddp;
    }
    if (dirlink(dp, name, ip->inum) < 0) {
        err = -28;
        goto baddp;
    }
    iunlock(dp);
    iput(dp);
    iput(ip);
    return 0;
baddp:
    iunlock(dp);
    iput(dp);
bad:
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlock(ip);
    iput(ip);
    return err;
}

int xv6fs_rename(char *oldp, char *newp) {
    struct inode *op, *np, *src, *dst = 0;
    char on[DIRSIZ], nn[DIRSIZ];
    struct dirent de;
    uint ooff, noff = 0;
    int err = 0;
    char oln[256], nln[256];
    int olen, nlen;
    if ((op = nameiparent(oldp, on)) == 0)
        return -2;
    for (int i = 0; i <= namei_lastlen; i++) oln[i] = namei_lastname[i];
    olen = namei_lastlen;
    if ((np = nameiparent(newp, nn)) == 0) {
        iput(op);
        return -2;
    }
    for (int i = 0; i <= namei_lastlen; i++) nln[i] = namei_lastname[i];
    nlen = namei_lastlen;
    if (op->dev == EXT2_DEV || np->dev == EXT2_DEV) {
        int r = -18;
        if (op->dev == np->dev) {
            if ((olen == 1 && oln[0] == '.') || (olen == 2 && oln[0] == '.' && oln[1] == '.') ||
                (nlen == 1 && nln[0] == '.') || (nlen == 2 && nln[0] == '.' && nln[1] == '.'))
                r = -22;
            else {
                ilock(op);
                if (np != op) ilock(np);
                r = ext2_rename(op, oln, olen, np, nln, nlen);
                if (np != op) iunlock(np);
                iunlock(op);
            }
        }
        iput(op); iput(np);
        return r;
    }
    if ((on[0] == '.' && (on[1] == 0 || (on[1] == '.' && on[2] == 0))) ||
        (nn[0] == '.' && (nn[1] == 0 || (nn[1] == '.' && nn[2] == 0)))) {
        iput(op); iput(np);
        return -22;
    }
    ilock(op);
    if (np != op)
        ilock(np);
    if ((src = dirlookup(op, on, &ooff)) == 0) {
        err = -2;
        goto out;
    }
    if (op == np && namecmp(on, nn) == 0) {
        iput(src);
        goto out;
    }
    if (src == np || src == op) {
        iput(src);
        err = -22;
        goto out;
    }
    ilock(src);
    dst = dirlookup(np, nn, &noff);
    if (dst == src) {
        iunlock(src); iput(src); iput(dst);
        goto out;
    }
    if (!dst && src->type == T_DIR && op != np) {
        iunlock(src); iput(src);
        err = -18;
        goto out;
    }
    if (dst) {
        if (dst == op || dst == np) {
            iunlock(src); iput(src); iput(dst);
            err = -39;
            goto out;
        }
        ilock(dst);
        if (src->type == T_DIR && dst->type != T_DIR)
            err = -20;
        else if (src->type != T_DIR && dst->type == T_DIR)
            err = -21;
        else if (dst->type == T_DIR) {
            uint o;
            for (o = 2 * sizeof(de); o < dst->size; o += sizeof(de))
                if (readi(dst, (char*)&de, o, sizeof(de)) != sizeof(de) || de.inum != 0) {
                    err = -39;
                    break;
                }
        }
        if (!err && src->type == T_DIR && op != np)
            err = -18;
        if (err) {
            iunlock(dst); iput(dst); iunlock(src); iput(src);
            goto out;
        }
        memset(&de, 0, sizeof(de));
        writei(np, (char*)&de, noff, sizeof(de));
        if (dst->type == T_DIR) {
            np->nlink--;
            iupdate(np);
        }
        dst->nlink--;
        iupdate(dst);
        iunlock(dst);
        iput(dst);
    }
    if (dirlink(np, nn, src->inum) < 0) {
        err = -28;
        iunlock(src); iput(src);
        goto out;
    }
    memset(&de, 0, sizeof(de));
    writei(op, (char*)&de, ooff, sizeof(de));
    iunlock(src);
    iput(src);
out:
    iunlock(op);
    if (np != op)
        iunlock(np);
    iput(op);
    iput(np);
    return err;
}
