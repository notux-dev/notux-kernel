#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "kfile.h"
#include "pmm.h"
#include "vmm.h"
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "Xjail/xv6/xv6fs_api.h"
#include "inet.h"
#include "net/net.h"
#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)
#define AF_UNIX 1
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 04000
#define SOCK_CLOEXEC 02000000
#define SOL_SOCKET 1
#define SCM_RIGHTS 1
#define SO_TYPE 3
#define SO_ERROR 4
#define SO_PEERCRED 17
#define MSG_PEEK 2
#define MSG_CTRUNC 8
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000
#define MSG_CMSG_CLOEXEC 0x40000000
#define NSOCK 128
#define RX_PAGES 4
#define RX_BYTES (RX_PAGES * 4096)
#define BACKLOG 16
#define NANC 16
#define MAXFDS_MSG 16
enum { SS_NEW, SS_LISTEN, SS_CONN };
struct ksock {
    int used, type, state;
    uint32_t gen;
    struct ksock *peer;
    uint32_t peer_gen;
    int peer_gone;
    int shut_rd, shut_wr;
    uint8_t *rx;
    uint32_t rpos, rcount;
    uint64_t wtotal, rtotal;
    struct inode *bip;
    char path[108];
    int pathlen;
    struct ksock *backlog[BACKLOG];
    int nback;
    struct { struct kfile *f; uint64_t at; } anc[NANC];
    int nanc;
    uint32_t pid;
    struct isock *in;
};
static struct ksock socks[NSOCK];
static struct ksock *sock_new(int type)
{
    for (int i = 0; i < NSOCK; i++)
        if (!socks[i].used)
        {
            struct ksock *s = &socks[i];
            uint32_t gen = s->gen;
            uint8_t *z = (uint8_t *)s;
            for (unsigned k = 0; k < sizeof *s; k++)
                z[k] = 0;
            s->gen = gen;
            s->rx = pmm_alloc_contig(RX_PAGES);
            if (!s->rx)
                return 0;
            s->used = 1;
            s->type = type;
            s->pid = get_current_process()->tgid;
            return s;
        }
    return 0;
}
static void sock_free(struct ksock *s)
{
    for (int i = 0; i < s->nanc; i++)
        kfile_put(s->anc[i].f);
    if (s->rx)
        pmm_free_contig(s->rx, RX_PAGES);
    if (s->bip)
        iput(s->bip);
    s->rx = 0;
    s->bip = 0;
    s->used = 0;
    s->gen++;
}
static struct kfile *sock_file(struct ksock *s, int nonblock)
{
    struct kfile *f = kfile_alloc(FT_SOCK, 2 | (nonblock ? O_NONBLOCK : 0), 0);
    if (f)
        f->sock = s;
    return f;
}
static struct ksock *sock_new_inet(struct isock *in, int type)
{
    struct ksock *s = sock_new(type);
    if (!s)
        return 0;
    pmm_free_contig(s->rx, RX_PAGES);
    s->rx = 0;
    s->in = in;
    return s;
}
static void *chan_of(struct ksock *s) { return s->in ? (void *)s->in : (void *)s; }
void *sock_chan(struct kfile *f) { return chan_of(f->sock); }
static struct ksock *get_sock(process_t *p, int64_t fd, struct kfile **fp, int *err)
{
    struct kfile *f = fd_get(p, fd);
    if (!f)
    {
        *err = -EBADF;
        return 0;
    }
    if (f->type != FT_SOCK)
    {
        *err = -ENOTSOCK;
        return 0;
    }
    if (fp)
        *fp = f;
    return f->sock;
}
void sock_close(struct kfile *f)
{
    struct ksock *s = f->sock;
    if (!s || !s->used)
        return;
    if (s->in)
    {
        inet_close(s->in);
        s->in = 0;
        sock_free(s);
        f->sock = 0;
        wakeup(&poll_chan);
        return;
    }
    if (s->state == SS_LISTEN)
        for (int i = 0; i < s->nback; i++)
        {
            struct ksock *b = s->backlog[i];
            if (b->peer)
            {
                b->peer->peer = 0;
                b->peer->peer_gone = 1;
                wakeup(b->peer);
            }
            sock_free(b);
        }
    if (s->type == SOCK_STREAM && s->peer)
    {
        s->peer->peer = 0;
        s->peer->peer_gone = 1;
        wakeup(s->peer);
    }
    sock_free(s);
    f->sock = 0;
    wakeup(&poll_chan);
}
static int get_addr(process_t *p, uint64_t uaddr, uint64_t len, char out[108], int *abstract)
{
    if (len < 3 || len > 110 || !uaddr)
        return -EINVAL;
    uint8_t buf[112];
    if (copy_from_user(p, buf, uaddr, len) < 0)
        return -EFAULT;
    if (buf[0] != AF_UNIX || buf[1] != 0)
        return -EAFNOSUPPORT;
    int n = (int)len - 2;
    for (int i = 0; i < 108; i++)
        out[i] = 0;
    if (buf[2] == 0)
    {
        *abstract = 1;
        for (int i = 0; i < n && i < 108; i++)
            out[i] = (char)buf[2 + i];
        return n;
    }
    *abstract = 0;
    int k = 0;
    while (k < n && k < 107 && buf[2 + k])
    {
        out[k] = (char)buf[2 + k];
        k++;
    }
    out[k] = 0;
    return k ? k : -EINVAL;
}
static struct ksock *find_by_abstract(const char *name, int n)
{
    for (int i = 0; i < NSOCK; i++)
        if (socks[i].used && socks[i].pathlen == n && socks[i].path[0] == 0)
        {
            int same = 1;
            for (int k = 0; k < n; k++)
                if (socks[i].path[k] != name[k])
                    same = 0;
            if (same)
                return &socks[i];
        }
    return 0;
}
static struct ksock *find_by_inode(struct inode *ip)
{
    for (int i = 0; i < NSOCK; i++)
        if (socks[i].used && socks[i].bip == ip)
            return &socks[i];
    return 0;
}
static struct ksock *lookup(process_t *p, uint64_t uaddr, uint64_t alen, int *err)
{
    char name[108];
    int abstract = 0;
    int n = get_addr(p, uaddr, alen, name, &abstract);
    if (n < 0)
    {
        *err = n;
        return 0;
    }
    struct ksock *d;
    if (abstract)
        d = find_by_abstract(name, n);
    else
    {
        struct inode *ip = namei(name);
        if (!ip)
        {
            *err = -ENOENT;
            return 0;
        }
        d = find_by_inode(ip);
        iput(ip);
    }
    if (!d)
        *err = -ECONNREFUSED;
    return d;
}
static int type_ok(int t) { return t == SOCK_STREAM || t == SOCK_DGRAM; }
uint64_t sys_socket(uint64_t *regs)
{
    process_t *p = get_current_process();
    int domain = (int)regs[9], type = (int)regs[10], proto = (int)regs[11];
    int flags = type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (domain != AF_UNIX && domain != AF_INET)
        RET(-EAFNOSUPPORT);
    if (!type_ok(type))
        RET(type == 5 ? -ESOCKTNOSUPPORT : -EPROTOTYPE);
    struct ksock *s;
    if (domain == AF_INET)
    {
        if (proto != 0 && proto != (type == SOCK_STREAM ? 6 : 17))
            RET(-EPROTONOSUPPORT);
        struct isock *in = inet_new(type);
        if (!in)
            RET(-ENOBUFS);
        s = sock_new_inet(in, type);
        if (!s)
        {
            inet_close(in);
            RET(-ENFILE);
        }
    }
    else
    {
        if (proto != 0)
            RET(-EPROTONOSUPPORT);
        s = sock_new(type);
    }
    if (!s)
        RET(-ENFILE);
    struct kfile *f = sock_file(s, flags & SOCK_NONBLOCK);
    if (!f)
    {
        sock_free(s);
        RET(-ENFILE);
    }
    int fd = fd_install(p, f, 0, (flags & SOCK_CLOEXEC) != 0);
    if (fd < 0)
        kfile_put(f);
    RET(fd);
}
uint64_t sys_socketpair(uint64_t *regs)
{
    process_t *p = get_current_process();
    int domain = (int)regs[9], type = (int)regs[10];
    int flags = type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (domain != AF_UNIX)
        RET(-EAFNOSUPPORT);
    if (!type_ok(type))
        RET(type == 5 ? -ESOCKTNOSUPPORT : -EPROTOTYPE);
    struct ksock *a = sock_new(type), *b = a ? sock_new(type) : 0;
    if (!b)
    {
        if (a)
            sock_free(a);
        RET(-ENFILE);
    }
    a->state = b->state = SS_CONN;
    a->peer = b;
    b->peer = a;
    a->peer_gen = b->gen;
    b->peer_gen = a->gen;
    struct kfile *fa = sock_file(a, flags & SOCK_NONBLOCK), *fb = sock_file(b, flags & SOCK_NONBLOCK);
    if (!fa || !fb)
    {
        if (fa) kfile_put(fa); else sock_free(a);
        if (fb) kfile_put(fb); else sock_free(b);
        RET(-ENFILE);
    }
    int cx = (flags & SOCK_CLOEXEC) != 0;
    int fda = fd_install(p, fa, 0, cx);
    if (fda < 0)
    {
        kfile_put(fa);
        kfile_put(fb);
        RET(fda);
    }
    int fdb = fd_install(p, fb, 0, cx);
    if (fdb < 0)
    {
        p->sh->fds[fda] = 0;
        kfile_put(fa);
        kfile_put(fb);
        RET(fdb);
    }
    int sv[2] = { fda, fdb };
    if (copy_to_user(p, regs[5], sv, sizeof sv) < 0)
    {
        p->sh->fds[fda] = 0;
        p->sh->fds[fdb] = 0;
        kfile_put(fa);
        kfile_put(fb);
        RET(-EFAULT);
    }
    RET(0);
}
uint64_t sys_bind(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], 0, &err);
    if (!s)
        RET(err);
    if (s->in)
        RET(inet_bind(p, s->in, regs[10], regs[11]));
    if (s->pathlen)
        RET(-EINVAL);
    char name[108];
    int abstract = 0;
    int n = get_addr(p, regs[10], regs[11], name, &abstract);
    if (n < 0)
        RET(n);
    if (abstract)
    {
        if (find_by_abstract(name, n))
            RET(-EADDRINUSE);
        for (int i = 0; i < 108; i++)
            s->path[i] = name[i];
        s->pathlen = n;
        RET(0);
    }
    struct inode *e = namei(name);
    if (e)
    {
        iput(e);
        RET(-EADDRINUSE);
    }
    extern int xv6fs_create_mode, xv6fs_errno;
    xv6fs_create_mode = 0755;
    struct inode *ip = xv6fs_create(name, T_FILE, 0, 0);
    xv6fs_create_mode = -1;
    if (!ip)
        RET(xv6fs_errno ? xv6fs_errno : -ENOENT);
    iunlock(ip);
    s->bip = ip;
    for (int i = 0; i < 108; i++)
        s->path[i] = name[i];
    s->pathlen = n;
    RET(0);
}
uint64_t sys_listen(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], 0, &err);
    if (!s)
        RET(err);
    if (s->in)
        RET(inet_listen(s->in, (int)regs[10]));
    if (s->type != SOCK_STREAM)
        RET(-EOPNOTSUPP);
    if (s->state == SS_CONN)
        RET(-EINVAL);
    if (!s->pathlen)
        RET(-EINVAL);
    s->state = SS_LISTEN;
    RET(0);
}
static uint64_t do_accept(uint64_t *regs, int flags)
{
    process_t *p = get_current_process();
    int err = 0;
    struct kfile *lf = 0;
    struct ksock *l = get_sock(p, (int64_t)regs[9], &lf, &err);
    if (!l)
        RET(err);
    if (l->in)
    {
        struct isock *c = inet_accept(l->in);
        if (!c)
        {
            if (lf->flags & O_NONBLOCK)
                RET(-EAGAIN);
            return sleep_on(regs, regs[14], l->in);
        }
        struct ksock *cs = sock_new_inet(c, SOCK_STREAM);
        struct kfile *cf = cs ? sock_file(cs, flags & SOCK_NONBLOCK) : 0;
        int cfd = cf ? fd_install(p, cf, 0, (flags & SOCK_CLOEXEC) != 0) : -ENFILE;
        if (cfd < 0)
        {
            if (cf)
                kfile_put(cf);
            else
            {
                if (cs)
                    sock_free(cs);
                inet_close(c);
            }
            RET(cfd);
        }
        if (regs[10] && regs[11])
        {
            uint32_t cap = 0, outlen;
            if (copy_from_user(p, &cap, regs[11], 4) == 0 &&
                inet_put_name(p, regs[10], cap, c->rip, c->rport, &outlen) == 0)
                copy_to_user(p, regs[11], &outlen, 4);
        }
        RET(cfd);
    }
    if (l->state != SS_LISTEN)
        RET(-EINVAL);
    if (l->nback == 0)
    {
        if (lf->flags & O_NONBLOCK)
            RET(-EAGAIN);
        return sleep_on(regs, regs[14], l);
    }
    struct ksock *s = l->backlog[0];
    for (int i = 1; i < l->nback; i++)
        l->backlog[i - 1] = l->backlog[i];
    l->nback--;
    struct kfile *f = sock_file(s, flags & SOCK_NONBLOCK);
    if (!f)
    {
        l->backlog[l->nback++] = s;
        RET(-ENFILE);
    }
    int fd = fd_install(p, f, 0, (flags & SOCK_CLOEXEC) != 0);
    if (fd < 0)
    {
        f->sock = 0;
        kfile_put(f);
        l->backlog[l->nback++] = s;
        RET(fd);
    }
    wakeup(l);
    if (regs[10] && regs[11])
    {
        uint32_t zero = 0;
        uint16_t fam = AF_UNIX;
        uint32_t len = 0;
        if (copy_from_user(p, &len, regs[11], 4) == 0)
        {
            if (len >= 2)
                copy_to_user(p, regs[10], &fam, 2);
            zero = 2;
            copy_to_user(p, regs[11], &zero, 4);
        }
    }
    RET(fd);
}
uint64_t sys_accept(uint64_t *regs) { return do_accept(regs, 0); }
uint64_t sys_accept4(uint64_t *regs) { return do_accept(regs, (int)regs[5]); }
uint64_t sys_connect(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct kfile *f = 0;
    struct ksock *c = get_sock(p, (int64_t)regs[9], &f, &err);
    if (!c)
        RET(err);
    if (c->in)
    {
        int64_t r = inet_connect(p, c->in, (f->flags & O_NONBLOCK) != 0, regs[10], regs[11]);
        if (r == E_BLOCK)
            return sleep_on(regs, regs[14], c->in);
        RET(r);
    }
    struct ksock *d = lookup(p, regs[10], regs[11], &err);
    if (!d)
        RET(err);
    if (c->type == SOCK_DGRAM)
    {
        if (d->type != SOCK_DGRAM)
            RET(-EPROTOTYPE);
        c->peer = d;
        c->peer_gen = d->gen;
        RET(0);
    }
    if (c->state == SS_CONN)
        RET(-EISCONN);
    if (c->state == SS_LISTEN)
        RET(-EINVAL);
    if (d->type != SOCK_STREAM || d->state != SS_LISTEN)
        RET(-ECONNREFUSED);
    if (d->nback >= BACKLOG)
    {
        if (f->flags & O_NONBLOCK)
            RET(-EAGAIN);
        return sleep_on(regs, regs[14], d);
    }
    struct ksock *s = sock_new(SOCK_STREAM);
    if (!s)
        RET(-ENOMEM);
    s->state = SS_CONN;
    s->pid = d->pid;
    s->peer = c;
    c->peer = s;
    c->state = SS_CONN;
    d->backlog[d->nback++] = s;
    wakeup(d);
    wakeup(&poll_chan);
    RET(0);
}
uint64_t sys_shutdown(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], 0, &err);
    if (!s)
        RET(err);
    int how = (int)regs[10];
    if (how < 0 || how > 2)
        RET(-EINVAL);
    if (s->in)
        RET(inet_shutdown(s->in, how));
    if (s->state != SS_CONN)
        RET(-ENOTCONN);
    if (how == 0 || how == 2)
        s->shut_rd = 1;
    if (how == 1 || how == 2)
    {
        s->shut_wr = 1;
        if (s->peer)
            wakeup(s->peer);
    }
    wakeup(s);
    wakeup(&poll_chan);
    RET(0);
}
static struct ksock *dest_of(struct ksock *s)
{
    if (s->type == SOCK_STREAM)
        return s->peer;
    if (s->peer && s->peer->used && s->peer->gen == s->peer_gen)
        return s->peer;
    return 0;
}
static void ring_put(struct ksock *d, const uint8_t *src, uint32_t n)
{
    uint32_t w = (d->rpos + d->rcount) % RX_BYTES;
    for (uint32_t i = 0; i < n; i++)
    {
        d->rx[w] = src[i];
        w = (w + 1) % RX_BYTES;
    }
    d->rcount += n;
    d->wtotal += n;
}
static void ring_get(struct ksock *s, uint8_t *dst, uint32_t n, int consume)
{
    uint32_t r = s->rpos;
    for (uint32_t i = 0; i < n; i++)
    {
        dst[i] = s->rx[r];
        r = (r + 1) % RX_BYTES;
    }
    if (consume)
    {
        s->rpos = r;
        s->rcount -= n;
        s->rtotal += n;
    }
}
static int64_t do_send(process_t *p, struct ksock *s, int nonblock, struct iov *iov, int niov,
                       struct kfile **fds, int nfds, int mflags, struct ksock *dgram_dest)
{
    if (s->shut_wr)
    {
        if (!(mflags & MSG_NOSIGNAL))
            signal_post(p, 13);
        return -EPIPE;
    }
    struct ksock *d = dgram_dest ? dgram_dest : dest_of(s);
    if (!d)
    {
        if (s->type == SOCK_STREAM)
        {
            if (s->state != SS_CONN)
                return -ENOTCONN;
            if (!(mflags & MSG_NOSIGNAL))
                signal_post(p, 13);
            return -EPIPE;
        }
        return -EDESTADDRREQ;
    }
    uint64_t total = 0;
    for (int i = 0; i < niov; i++)
        total += iov[i].len;
    if (s->type == SOCK_STREAM && d->shut_rd)
    {
        if (!(mflags & MSG_NOSIGNAL))
            signal_post(p, 13);
        return -EPIPE;
    }
    uint32_t room = RX_BYTES - d->rcount;
    uint32_t take;
    if (s->type == SOCK_DGRAM)
    {
        if (total + 4 > RX_BYTES)
            return -EMSGSIZE;
        if (room < total + 4)
            return nonblock ? -EAGAIN : E_BLOCK;
        take = (uint32_t)total;
        uint32_t len = take;
        ring_put(d, (uint8_t *)&len, 4);
    }
    else
    {
        if (total == 0 && nfds == 0)
            return 0;
        if (room == 0)
            return nonblock ? -EAGAIN : E_BLOCK;
        take = total < room ? (uint32_t)total : room;
        if (nfds && d->nanc + nfds > NANC)
            return -ENOBUFS;
    }
    uint64_t base = d->wtotal;
    uint32_t done = 0;
    uint8_t tmp[512];
    for (int i = 0; i < niov && done < take; i++)
    {
        uint64_t off = 0;
        while (off < iov[i].len && done < take)
        {
            uint32_t c = (uint32_t)(iov[i].len - off);
            if (c > sizeof tmp)
                c = sizeof tmp;
            if (c > take - done)
                c = take - done;
            if (copy_from_user(p, tmp, iov[i].base + off, c) < 0)
            {
                if (done || s->type == SOCK_DGRAM)
                    goto out;
                return -EFAULT;
            }
            ring_put(d, tmp, c);
            off += c;
            done += c;
        }
    }
out:
    if (nfds && done)
        for (int i = 0; i < nfds; i++)
        {
            fds[i]->refs++;
            d->anc[d->nanc].f = fds[i];
            d->anc[d->nanc].at = base;
            d->nanc++;
        }
    wakeup(d);
    wakeup(&poll_chan);
    return done;
}
static int64_t do_recv(process_t *p, struct ksock *s, int nonblock, struct iov *iov, int niov, int mflags,
                       struct kfile **got, int *ngot, int maxfds)
{
    *ngot = 0;
    if (s->rcount == 0)
    {
        if (s->type == SOCK_STREAM && s->state != SS_CONN)
            return -ENOTCONN;
        if (s->shut_rd || s->peer_gone || (s->type == SOCK_STREAM && s->peer && s->peer->shut_wr))
            return 0;
        return nonblock ? -EAGAIN : E_BLOCK;
    }
    uint64_t cap = 0;
    for (int i = 0; i < niov; i++)
        cap += iov[i].len;
    int peek = (mflags & MSG_PEEK) != 0;
    uint32_t avail, msglen = 0;
    if (s->type == SOCK_DGRAM)
    {
        uint8_t hdr[4];
        ring_get(s, hdr, 4, 0);
        msglen = *(uint32_t *)hdr;
        avail = msglen;
    }
    else
    {
        avail = s->rcount;
        for (int i = 0; i < s->nanc; i++)
            if (s->anc[i].at > s->rtotal && s->anc[i].at - s->rtotal < avail)
                avail = (uint32_t)(s->anc[i].at - s->rtotal);
    }
    uint32_t take = cap < avail ? (uint32_t)cap : avail;
    if (s->type == SOCK_DGRAM && !peek)
    {
        uint8_t hdr[4];
        ring_get(s, hdr, 4, 1);
    }
    uint32_t off_in_ring = 0;
    uint32_t done = 0;
    uint8_t tmp[512];
    for (int i = 0; i < niov && done < take; i++)
    {
        uint64_t o = 0;
        while (o < iov[i].len && done < take)
        {
            uint32_t c = (uint32_t)(iov[i].len - o);
            if (c > sizeof tmp)
                c = sizeof tmp;
            if (c > take - done)
                c = take - done;
            if (peek)
            {
                uint32_t r = (s->rpos + (s->type == SOCK_DGRAM ? 4 : 0) + off_in_ring) % RX_BYTES;
                for (uint32_t k = 0; k < c; k++)
                    tmp[k] = s->rx[(r + k) % RX_BYTES];
                off_in_ring += c;
            }
            else
                ring_get(s, tmp, c, 0);
            if (copy_to_user(p, iov[i].base + o, tmp, c) < 0)
                return done ? (int64_t)done : -EFAULT;
            if (!peek)
            {
                s->rpos = (s->rpos + c) % RX_BYTES;
                s->rcount -= c;
                s->rtotal += c;
            }
            o += c;
            done += c;
        }
    }
    if (s->type == SOCK_DGRAM && !peek && take < avail)
    {
        uint32_t rest = avail - take;
        s->rpos = (s->rpos + rest) % RX_BYTES;
        s->rcount -= rest;
        s->rtotal += rest;
    }
    if (!peek && s->type == SOCK_STREAM)
    {
        uint64_t first = s->rtotal - done;
        int keep = 0;
        for (int i = 0; i < s->nanc; i++)
        {
            if (s->anc[i].at < s->rtotal && s->anc[i].at >= first && done)
            {
                if (*ngot < maxfds)
                    got[(*ngot)++] = s->anc[i].f;
                else
                    kfile_put(s->anc[i].f);
            }
            else
                s->anc[keep++] = s->anc[i];
        }
        s->nanc = keep;
    }
    if (!peek)
    {
        if (s->peer && s->type == SOCK_STREAM)
            wakeup(s->peer);
        wakeup(&poll_chan);
    }
    return done;
}
int64_t sock_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    struct iov v = { ubuf, n };
    if (f->sock->in)
        return inet_send(p, f->sock->in, (f->flags & O_NONBLOCK) != 0, &v, 1, 0, 0, 0);
    return do_send(p, f->sock, (f->flags & O_NONBLOCK) != 0, &v, 1, 0, 0, 0, 0);
}
int64_t sock_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    struct iov v = { ubuf, n };
    struct kfile *got[MAXFDS_MSG];
    int ng = 0;
    if (f->sock->in)
    {
        uint32_t ip;
        uint16_t port;
        return inet_recv(p, f->sock->in, (f->flags & O_NONBLOCK) != 0, &v, 1, 0, &ip, &port);
    }
    int64_t r = do_recv(p, f->sock, (f->flags & O_NONBLOCK) != 0, &v, 1, 0, got, &ng, 0);
    return r;
}
int sock_poll(struct kfile *f)
{
    struct ksock *s = f->sock;
    int r = 0;
    if (s->in)
        return inet_poll(s->in);
    if (s->state == SS_LISTEN)
        return s->nback ? 1 : 0;
    if (s->rcount)
        r |= 1;
    else if (s->shut_rd || s->peer_gone)
        r |= 1 | 0x10;
    struct ksock *d = dest_of(s);
    if (s->type == SOCK_DGRAM)
        r |= 4;
    else if (d && !s->shut_wr && d->rcount < RX_BYTES)
        r |= 4;
    else if (s->state == SS_CONN && !d)
        r |= 8;
    return r;
}
int sock_nread(struct kfile *f)
{
    struct ksock *s = f->sock;
    if (s->in)
        return inet_nread(s->in);
    if (s->type == SOCK_DGRAM && s->rcount >= 4)
        return (int)*(uint32_t *)&s->rx[s->rpos];
    return (int)s->rcount;
}
static int get_iov(process_t *p, uint64_t uiov, uint64_t cnt, struct iov *out)
{
    if (cnt > 16)
        return -EMSGSIZE;
    if (cnt && copy_from_user(p, out, uiov, cnt * sizeof(struct iov)) < 0)
        return -EFAULT;
    return 0;
}
uint64_t sys_sendto(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct kfile *f = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], &f, &err);
    if (!s)
        RET(err);
    if (s->in)
    {
        struct iov v = { regs[10], regs[11] };
        int mf = (int)regs[5];
        int64_t r = inet_send(p, s->in, (f->flags & O_NONBLOCK) || (mf & MSG_DONTWAIT), &v, 1, mf,
                              regs[7], regs[6]);
        if (r == E_BLOCK)
            return sleep_on(regs, regs[14], s->in);
        RET(r);
    }
    struct ksock *dest = 0;
    if (regs[7] && regs[6])
    {
        dest = lookup(p, regs[7], regs[6], &err);
        if (!dest)
            RET(err);
        if (s->type == SOCK_STREAM)
            RET(-EISCONN);
    }
    struct iov v = { regs[10], regs[11] };
    int mf = (int)regs[5];
    int64_t r = do_send(p, s, (f->flags & O_NONBLOCK) || (mf & MSG_DONTWAIT), &v, 1, 0, 0, mf, dest);
    if (r == E_BLOCK)
        return sleep_on(regs, regs[14], s);
    RET(r);
}
uint64_t sys_recvfrom(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct kfile *f = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], &f, &err);
    if (!s)
        RET(err);
    struct iov v = { regs[10], regs[11] };
    int mf = (int)regs[5];
    if (s->in)
    {
        uint32_t ip = 0, outlen, cap = 0;
        uint16_t port = 0;
        int64_t r = inet_recv(p, s->in, (f->flags & O_NONBLOCK) || (mf & MSG_DONTWAIT), &v, 1, mf, &ip, &port);
        if (r == E_BLOCK)
            return sleep_on(regs, regs[14], s->in);
        if (r >= 0 && regs[7] && regs[6] && copy_from_user(p, &cap, regs[6], 4) == 0 &&
            inet_put_name(p, regs[7], cap, ip, port, &outlen) == 0)
            copy_to_user(p, regs[6], &outlen, 4);
        RET(r);
    }
    struct kfile *got[MAXFDS_MSG];
    int ng = 0;
    int64_t r = do_recv(p, s, (f->flags & O_NONBLOCK) || (mf & MSG_DONTWAIT), &v, 1, mf, got, &ng, 0);
    if (r == E_BLOCK)
        return sleep_on(regs, regs[14], s);
    if (r >= 0 && regs[7] && regs[6])
    {
        uint32_t zero = 0;
        copy_to_user(p, regs[6], &zero, 4);
    }
    RET(r);
}
struct msghdr_k {
    uint64_t name; uint32_t namelen, _p0;
    uint64_t iov, iovlen;
    uint64_t control, controllen;
    int flags, _p1;
};
struct cmsg_k { uint64_t len; int level, type; };
uint64_t sys_sendmsg(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct kfile *f = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], &f, &err);
    if (!s)
        RET(err);
    struct msghdr_k m;
    if (copy_from_user(p, &m, regs[10], sizeof m) < 0)
        RET(-EFAULT);
    struct iov iov[16];
    int e = get_iov(p, m.iov, m.iovlen, iov);
    if (e < 0)
        RET(e);
    if (s->in)
    {
        int mf = (int)regs[11];
        int64_t r = inet_send(p, s->in, (f->flags & O_NONBLOCK) || (mf & MSG_DONTWAIT), iov, (int)m.iovlen, mf,
                              m.name, m.namelen);
        if (r == E_BLOCK)
            return sleep_on(regs, regs[14], s->in);
        RET(r);
    }
    struct ksock *dest = 0;
    if (m.name && m.namelen)
    {
        dest = lookup(p, m.name, m.namelen, &err);
        if (!dest)
            RET(err);
        if (s->type == SOCK_STREAM)
            RET(-EISCONN);
    }
    struct kfile *fds[MAXFDS_MSG];
    int nfds = 0;
    if (m.control && m.controllen >= sizeof(struct cmsg_k))
    {
        uint8_t cbuf[256];
        uint64_t clen = m.controllen > sizeof cbuf ? sizeof cbuf : m.controllen;
        if (copy_from_user(p, cbuf, m.control, clen) < 0)
            RET(-EFAULT);
        uint64_t off = 0;
        while (off + sizeof(struct cmsg_k) <= clen)
        {
            struct cmsg_k *c = (struct cmsg_k *)(cbuf + off);
            if (c->len < sizeof(struct cmsg_k) || off + c->len > clen)
                RET(-EINVAL);
            if (c->level == SOL_SOCKET && c->type == SCM_RIGHTS)
            {
                int cnt = (int)((c->len - sizeof(struct cmsg_k)) / 4);
                int *fd = (int *)(cbuf + off + sizeof(struct cmsg_k));
                for (int i = 0; i < cnt; i++)
                {
                    struct kfile *kf = fd_get(p, fd[i]);
                    if (!kf)
                        RET(-EBADF);
                    if (nfds >= MAXFDS_MSG)
                        RET(-EINVAL);
                    fds[nfds++] = kf;
                }
            }
            off += (c->len + 7) & ~7ULL;
        }
    }
    int mf = (int)regs[11];
    int64_t r = do_send(p, s, (f->flags & O_NONBLOCK) || (mf & MSG_DONTWAIT), iov, (int)m.iovlen,
                        fds, nfds, mf, dest);
    if (r == E_BLOCK)
        return sleep_on(regs, regs[14], s);
    RET(r);
}
uint64_t sys_recvmsg(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct kfile *f = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], &f, &err);
    if (!s)
        RET(err);
    struct msghdr_k m;
    if (copy_from_user(p, &m, regs[10], sizeof m) < 0)
        RET(-EFAULT);
    struct iov iov[16];
    int e = get_iov(p, m.iov, m.iovlen, iov);
    if (e < 0)
        RET(e);
    int mf = (int)regs[11];
    if (s->in)
    {
        uint32_t ip = 0, outlen = 0;
        uint16_t port = 0;
        int64_t r = inet_recv(p, s->in, (f->flags & O_NONBLOCK) || (mf & MSG_DONTWAIT), iov, (int)m.iovlen, mf,
                              &ip, &port);
        if (r == E_BLOCK)
            return sleep_on(regs, regs[14], s->in);
        if (r < 0)
            RET(r);
        if (m.name && m.namelen && inet_put_name(p, m.name, m.namelen, ip, port, &outlen) < 0)
            RET(-EFAULT);
        uint64_t zero64 = 0;
        int zero32 = 0;
        copy_to_user(p, regs[10] + 8, &outlen, 4);
        copy_to_user(p, regs[10] + 40, &zero64, 8);
        copy_to_user(p, regs[10] + 48, &zero32, 4);
        RET(r);
    }
    int maxfds = 0;
    if (m.control && m.controllen >= sizeof(struct cmsg_k) + 4)
    {
        maxfds = (int)((m.controllen - sizeof(struct cmsg_k)) / 4);
        if (maxfds > MAXFDS_MSG)
            maxfds = MAXFDS_MSG;
    }
    struct kfile *got[MAXFDS_MSG];
    int ng = 0;
    int64_t r = do_recv(p, s, (f->flags & O_NONBLOCK) || (mf & MSG_DONTWAIT), iov, (int)m.iovlen, mf,
                        got, &ng, maxfds);
    if (r == E_BLOCK)
        return sleep_on(regs, regs[14], s);
    if (r < 0)
        RET(r);
    int outflags = 0;
    uint64_t clen = 0;
    if (ng)
    {
        int fdv[MAXFDS_MSG], k = 0;
        for (int i = 0; i < ng; i++)
        {
            int nfd = fd_install(p, got[i], 0, (mf & MSG_CMSG_CLOEXEC) != 0);
            if (nfd < 0)
            {
                kfile_put(got[i]);
                outflags |= MSG_CTRUNC;
            }
            else
                fdv[k++] = nfd;
        }
        if (k)
        {
            struct cmsg_k c = { sizeof(struct cmsg_k) + 4 * (uint64_t)k, SOL_SOCKET, SCM_RIGHTS };
            if (copy_to_user(p, m.control, &c, sizeof c) < 0 ||
                copy_to_user(p, m.control + sizeof c, fdv, 4 * (uint64_t)k) < 0)
                RET(-EFAULT);
            clen = (c.len + 7) & ~7ULL;
        }
    }
    uint32_t zero = 0;
    copy_to_user(p, regs[10] + 8, &zero, 4);
    copy_to_user(p, regs[10] + 40, &clen, 8);
    copy_to_user(p, regs[10] + 48, &outflags, 4);
    RET(r);
}
static int64_t inet_name(process_t *p, struct isock *in, int peer, uint64_t uaddr, uint64_t ulen)
{
    uint32_t ip, cap = 0, outlen;
    uint16_t port;
    int e = inet_sockname(in, peer, &ip, &port);
    if (e < 0)
        return e;
    if (copy_from_user(p, &cap, ulen, 4) < 0)
        return -EFAULT;
    if (inet_put_name(p, uaddr, cap, ip, port, &outlen) < 0 || copy_to_user(p, ulen, &outlen, 4) < 0)
        return -EFAULT;
    return 0;
}
static uint64_t put_name(process_t *p, struct ksock *s, uint64_t uaddr, uint64_t ulen)
{
    uint8_t out[112];
    for (int i = 0; i < 112; i++)
        out[i] = 0;
    out[0] = AF_UNIX;
    uint32_t len = 2;
    if (s && s->pathlen)
    {
        for (int i = 0; i < s->pathlen && i < 108; i++)
            out[2 + i] = (uint8_t)s->path[i];
        len = 2 + (uint32_t)s->pathlen + (s->path[0] ? 1 : 0);
    }
    uint32_t cap = 0;
    if (copy_from_user(p, &cap, ulen, 4) < 0)
        return (uint64_t)-EFAULT;
    uint32_t n = len < cap ? len : cap;
    if (n && copy_to_user(p, uaddr, out, n) < 0)
        return (uint64_t)-EFAULT;
    if (copy_to_user(p, ulen, &len, 4) < 0)
        return (uint64_t)-EFAULT;
    return 0;
}
uint64_t sys_getsockname(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], 0, &err);
    if (!s)
        RET(err);
    if (s->in)
        RET(inet_name(p, s->in, 0, regs[10], regs[11]));
    RET((int64_t)put_name(p, s, regs[10], regs[11]));
}
uint64_t sys_getpeername(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], 0, &err);
    if (!s)
        RET(err);
    if (s->in)
        RET(inet_name(p, s->in, 1, regs[10], regs[11]));
    if (s->state != SS_CONN)
        RET(-ENOTCONN);
    RET((int64_t)put_name(p, dest_of(s), regs[10], regs[11]));
}
uint64_t sys_setsockopt(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    if (!get_sock(p, (int64_t)regs[9], 0, &err))
        RET(err);
    RET(0);
}
uint64_t sys_getsockopt(uint64_t *regs)
{
    process_t *p = get_current_process();
    int err = 0;
    struct ksock *s = get_sock(p, (int64_t)regs[9], 0, &err);
    if (!s)
        RET(err);
    int level = (int)regs[10], opt = (int)regs[11];
    uint64_t uval = regs[5], ulen = regs[7];
    uint32_t cap = 0;
    if (!uval || !ulen || copy_from_user(p, &cap, ulen, 4) < 0)
        RET(-EFAULT);
    uint8_t out[16];
    uint32_t n = 4;
    for (int i = 0; i < 16; i++)
        out[i] = 0;
    if (s->in)
        inet_getsockopt(s->in, level, opt, out, &n);
    else if (level != SOL_SOCKET)
        RET(-ENOPROTOOPT);
    if (s->in)
        ;
    else if (opt == SO_TYPE)
        *(int *)out = s->type;
    else if (opt == SO_ERROR)
        *(int *)out = 0;
    else if (opt == SO_PEERCRED)
    {
        struct ksock *d = dest_of(s);
        ((int *)out)[0] = (int)(d ? d->pid : s->pid);
        n = 12;
    }
    else
        *(int *)out = 0;
    if (n > cap)
        n = cap;
    if (copy_to_user(p, uval, out, n) < 0 || copy_to_user(p, ulen, &n, 4) < 0)
        RET(-EFAULT);
    RET(0);
}
