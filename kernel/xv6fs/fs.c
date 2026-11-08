#include "../Xjail/xv6/types.h"
#include "../Xjail/xv6/defs.h"
#include "../Xjail/xv6/param.h"
#include "../Xjail/xv6/stat.h"
#include "../Xjail/xv6/mmu.h"
#include "../Xjail/xv6/proc.h"
#include "../Xjail/xv6/spinlock.h"
#include "../Xjail/xv6/sleeplock.h"
#include "../Xjail/xv6/fs.h"
#include "../Xjail/xv6/buf.h"
#include "../Xjail/xv6/file.h"
#include "../ext2/ext2.h"
#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
struct superblock sb;
int xv6fs_mounted = 0;
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;
  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}
static void
bzero(int dev, int bno)
{
  struct buf *bp;
  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}
static uint
balloc(uint dev)
{
  int b, bi, m;
  if (!xv6fs_mounted)
    panic("xv6fs: balloc() with no valid superblock mounted -- "
          "refusing to allocate/write on this disk");
  struct buf *bp;
  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  
        bp->data[bi/8] |= m;  
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;
  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}
struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;
int
xv6fs_iinit(void)
{
  int i = 0;
  int dev = ROOTDEV;
  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }
  readsb(dev, &sb);
  if (sb.magic != XV6_MAGIC) {
    cprintf("xv6fs: sector 1 magic mismatch (got 0x%x, want 0x%x) --"
            " not mounting, disk left untouched\n", sb.magic, XV6_MAGIC);
    return -1;
  }
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
 inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
  xv6fs_mounted = 1;
  return 0;
}
struct inode* iget(uint dev, uint inum);
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;
  if (!xv6fs_mounted)
    panic("xv6fs: ialloc() with no valid superblock mounted -- "
          "refusing to allocate/write on this disk");
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}
void
iupdate(struct inode *ip)
{
  if(ip->dev == EXT2_DEV){
    ext2_iupdate(ip);
    return;
  }
  struct buf *bp;
  struct dinode *dip;
  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}
struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;
  acquire(&icache.lock);
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    
      empty = ip;
  }
  if(empty == 0)
    panic("iget: no inodes");
  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);
  return ip;
}
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;
  if(ip == 0 || ip->ref < 1)
    panic("ilock");
  acquiresleep(&ip->lock);
  if(ip->valid == 0 && ip->dev == EXT2_DEV){
    if(ext2_load(ip) < 0)
      panic("ilock: ext2 inode read");
    ip->valid = 1;
  }
  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");
  releasesleep(&ip->lock);
}
void
iput(struct inode *ip)
{
  acquiresleep(&ip->lock);
  if(ip->dev == EXT2_DEV){
    if(ip->valid && ip->nlink == 0 && ip->ref == 1)
      ext2_evict(ip);
    releasesleep(&ip->lock);
    acquire(&icache.lock);
    ip->ref--;
    release(&icache.lock);
    return;
  }
  if(ip->valid && ip->nlink == 0){
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);
    if(r == 1){
      itrunc(ip);
      ip->type = 0;
      iupdate(ip);
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);
  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;
  if(bn < NINDIRECT){
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;
  if(bn < NDINDIRECT){
    uint *a2;
    struct buf *bp2;
    uint idx1 = bn / NINDIRECT; 
    uint idx2 = bn % NINDIRECT; 
    if((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[idx1]) == 0){
      a[idx1] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    bp2 = bread(ip->dev, addr);
    a2 = (uint*)bp2->data;
    if((addr = a2[idx2]) == 0){
      a2[idx2] = addr = balloc(ip->dev);
      log_write(bp2);
    }
    brelse(bp2);
    return addr;
  }
  panic("bmap: out of range");
}
static void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
  if(ip->addrs[NDIRECT+1]){
    struct buf *bp2;
    uint *a2;
    int k;
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j]){
        bp2 = bread(ip->dev, a[j]);
        a2 = (uint*)bp2->data;
        for(k = 0; k < NINDIRECT; k++){
          if(a2[k])
            bfree(ip->dev, a2[k]);
        }
        brelse(bp2);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }
  ip->size = 0;
  iupdate(ip);
}
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;
  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }
  if(ip->dev == EXT2_DEV)
    return ext2_readi(ip, dst, off, n);
  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;
  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  if(ip->dev == EXT2_DEV)
    return ext2_writei(ip, src, off, n);
  uint tot, m;
  struct buf *bp;
  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }
  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;
  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    log_write(bp);
    brelse(bp);
  }
  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}
int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;
  if(dp->type != T_DIR)
    panic("dirlookup not DIR");
  if(dp->dev == EXT2_DEV){
    int l = 0;
    while(name[l]) l++;
    return ext2_dirlookup(dp, name, l);
  }
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }
  return 0;
}
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }
  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");
  return 0;
}
static int name_toolong;
int namei_nofollow;
int namei_enotdir;
char namei_lastname[256];
int  namei_lastlen;

#define MAXNAME 255
#define MAXSYMLINK 8
static char*
skipelem(char *path, char *name, int *outlen)
{
  char *s;
  int len;
  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len > MAXNAME)
    len = MAXNAME + 1;
  *outlen = len;
  if(len > MAXNAME){
    name[0] = 0;
    while(*path == '/')
      path++;
    return path;
  }
  memmove(name, s, len);
  name[len] = 0;
  while(*path == '/')
    path++;
  return path;
}
static struct inode*
mount_cross_down(struct inode *next)
{
  int mi = ext2_mountpoint();
  if(mi && next->dev == ROOTDEV && next->inum == (uint)mi){
    iput(next);
    return iget(EXT2_DEV, EXT2_ROOT_INO);
  }
  uint bi = ext2_bind_ino();
  if(bi && next->dev == ROOTDEV && next->inum == ext2_bind_mp()){
    iput(next);
    return iget(EXT2_DEV, bi);
  }
  return next;
}
static struct inode *
fs_rootdir(void)
{
  return ext2_is_root() ? iget(EXT2_DEV, EXT2_ROOT_INO) : iget(ROOTDEV, ROOTINO);
}
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;
  static char sym[MAXSYMLINK][512];
  char full[MAXNAME + 2];
  char tgt[512];
  int nlen, links = 0;
  name_toolong = 0;
  namei_enotdir = 0;
  if(*path == '/')
    ip = fs_rootdir();
  else
    ip = idup(myproc()->cwd);
  ip = mount_cross_down(ip);
  while((path = skipelem(path, full, &nlen)) != 0){
    int last = (*path == '\0');
    ilock(ip);
    if(ip->type != T_DIR){
      namei_enotdir = 1;
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && last){
      if(nlen > DIRSIZ) name_toolong = 1;
      if(nlen <= MAXNAME)
        memmove(namei_lastname, full, nlen + 1);
      else
        namei_lastname[0] = 0;
      namei_lastlen = nlen;
      memmove(name, full, nlen >= DIRSIZ ? DIRSIZ : nlen + 1);
      if(nlen < DIRSIZ) name[nlen] = 0;
      iunlock(ip);
      return ip;
    }
    if(ip->dev != EXT2_DEV && nlen > DIRSIZ){
      name_toolong = 1;
      iunlockput(ip);
      return 0;
    }
    if(nlen > MAXNAME){
      iunlockput(ip);
      return 0;
    }
    if(ip->dev == EXT2_DEV && ip->inum == EXT2_ROOT_INO && ext2_mountpoint() && nlen == 2 &&
       full[0] == '.' && full[1] == '.'){
      struct inode *mp = iget(ROOTDEV, (uint)ext2_mountpoint());
      ilock(mp);
      next = dirlookup(mp, "..", 0);
      iunlockput(mp);
    } else if(ip->dev == EXT2_DEV && ext2_bind_ino() && ip->inum == ext2_bind_ino() &&
              nlen == 2 && full[0] == '.' && full[1] == '.'){
      struct inode *mp = iget(ROOTDEV, ext2_bind_mp());
      ilock(mp);
      next = dirlookup(mp, "..", 0);
      iunlockput(mp);
    } else if(ip->dev == EXT2_DEV)
      next = ext2_dirlookup(ip, full, nlen);
    else
      next = dirlookup(ip, full, 0);
    if(next == 0){
      iunlockput(ip);
      return 0;
    }
    if(ip->dev != EXT2_DEV)
      next = mount_cross_down(next);
    iunlock(ip);
    if(next->dev == EXT2_DEV){
      ilock(next);
      int is_lnk = (next->type == T_LNK);
      iunlock(next);
      if(is_lnk && !(last && namei_nofollow)){
        if(++links > MAXSYMLINK){
          iput(next); iput(ip);
          return 0;
        }
        ilock(next);
        int tl = ext2_readlink(next, tgt, 511);
        iunlock(next);
        iput(next);
        if(tl <= 0){ iput(ip); return 0; }
        tgt[tl] = 0;
        char *buf = sym[links - 1];
        int n = 0;
        for(int i = 0; i < tl && n < 500; i++) buf[n++] = tgt[i];
        buf[n++] = '/';
        for(char *q = path; *q && n < 510; q++) buf[n++] = *q;
        buf[n] = 0;
        if(tgt[0] == '/'){
          iput(ip);
          ip = mount_cross_down(fs_rootdir());
        }
        path = buf;
        continue;
      }
    }
    iput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}
struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}
struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}


static void
bfree_from(struct inode *ip, uint first)
{
  uint nb = (ip->size + BSIZE - 1) / BSIZE;
  uint bn;
  for(bn = first; bn < nb; bn++){
    if(bn < NDIRECT){
      if(ip->addrs[bn]){
        bfree(ip->dev, ip->addrs[bn]);
        ip->addrs[bn] = 0;
      }
    } else if(bn - NDIRECT < NINDIRECT){
      uint addr = ip->addrs[NDIRECT], *a;
      struct buf *bp;
      if(!addr)
        continue;
      bp = bread(ip->dev, addr);
      a = (uint*)bp->data;
      if(a[bn - NDIRECT]){
        bfree(ip->dev, a[bn - NDIRECT]);
        a[bn - NDIRECT] = 0;
        log_write(bp);
      }
      brelse(bp);
    } else {
      uint b2 = bn - NDIRECT - NINDIRECT;
      uint idx1 = b2 / NINDIRECT, idx2 = b2 % NINDIRECT;
      uint addr = ip->addrs[NDIRECT+1], l2, *a, *a2;
      struct buf *bp, *bp2;
      if(!addr)
        continue;
      bp = bread(ip->dev, addr);
      a = (uint*)bp->data;
      l2 = a[idx1];
      brelse(bp);
      if(!l2)
        continue;
      bp2 = bread(ip->dev, l2);
      a2 = (uint*)bp2->data;
      if(a2[idx2]){
        bfree(ip->dev, a2[idx2]);
        a2[idx2] = 0;
        log_write(bp2);
      }
      brelse(bp2);
    }
  }
}

int
xv6fs_truncate(struct inode *ip, uint len)
{
  if(ip->dev == EXT2_DEV)
    return ext2_truncate(ip, len);
  if(!xv6fs_mounted)
    return -5;
  if(len > (uint)MAXFILE * BSIZE)
    return -27;
  if(len == ip->size)
    return 0;
  if(len == 0){
    itrunc(ip);
    return 0;
  }
  if(len < ip->size){
    bfree_from(ip, (len + BSIZE - 1) / BSIZE);
    if(len % BSIZE){
      struct buf *bp = bread(ip->dev, bmap(ip, len / BSIZE));
      memset(bp->data + len % BSIZE, 0, BSIZE - len % BSIZE);
      log_write(bp);
      brelse(bp);
    }
    ip->size = len;
    iupdate(ip);
    return 0;
  }
  {
    char zero[BSIZE];
    uint pos = ip->size;
    memset(zero, 0, sizeof(zero));
    while(pos < len){
      uint n = len - pos;
      if(n > BSIZE)
        n = BSIZE;
      if(writei(ip, zero, pos, n) != (int)n)
        return -28;
      pos += n;
    }
  }
  return 0;
}

void
xv6fs_statfs(uint *blocks, uint *bfree_out, uint *files, uint *ffree)
{
  uint b, bi, nfree = 0, inum, nfi = 0;
  struct buf *bp;
  if(!xv6fs_mounted){
    *blocks = *bfree_out = *files = *ffree = 0;
    return;
  }
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(ROOTDEV, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++)
      if((bp->data[bi/8] & (1 << (bi % 8))) == 0)
        nfree++;
    brelse(bp);
  }
  for(inum = 1; inum < sb.ninodes; inum++){
    struct dinode *dip;
    bp = bread(ROOTDEV, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum % IPB;
    if(dip->type == 0)
      nfi++;
    brelse(bp);
  }
  *blocks = sb.size;
  *bfree_out = nfree;
  *files = sb.ninodes;
  *ffree = nfi;
}
