#include "types.h"   
#include "fs.h"      
#include "file.h"    
#include "defs.h"    
#include "xv6fs_api.h"
struct inode* nameiparent(char *path, char *name);
struct inode* dirlookup(struct inode *dp, char *name, uint *off);
int dirlink(struct inode *dp, char *name, uint inum);
struct inode* ialloc(uint dev, short type);
struct inode* xv6fs_create(char *path, short type, short major, short minor) {
    struct inode *ip, *dp;
    char name[DIRSIZ];
    if ((dp = nameiparent(path, name)) == 0)
        return 0;
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
int xv6fs_unlink(char *path) {
    struct inode *ip, *dp;
    struct dirent de;
    char name[DIRSIZ];
    uint off;
    if ((dp = nameiparent(path, name)) == 0)
        return -1;
    ilock(dp);
    if (name[0] == '.' && name[1] == 0) {
        iunlock(dp);
        iput(dp);
        return -1;
    }
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        iunlock(dp);
        iput(dp);
        return -1;
    }
    if ((ip = dirlookup(dp, name, &off)) == 0) {
        iunlock(dp);
        iput(dp);
        return -1;
    }
    ilock(ip);
    if (ip->nlink < 1) {
        iunlock(ip);
        iput(ip);
        iunlock(dp);
        iput(dp);
        return -1;
    }
    if (ip->type == T_DIR) {
        int empty = 1;
        uint o;
        for (o = 2 * sizeof(de); o < ip->size; o += sizeof(de)) {
            if (readi(ip, (char*)&de, o, sizeof(de)) != sizeof(de)) {
                empty = 0;
                break;
            }
            if (de.inum != 0) {
                empty = 0;
                break;
            }
        }
        if (!empty) {
            iunlock(ip);
            iput(ip);
            iunlock(dp);
            iput(dp);
            return -1;
        }
    }
    memset(&de, 0, sizeof(de));
    if (writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de)) {
        iunlock(ip);
        iput(ip);
        iunlock(dp);
        iput(dp);
        return -1;
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
