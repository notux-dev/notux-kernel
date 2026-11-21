#include "inet.h"
#include "syscalls.h"
#include "perm.h"
#include "uaccess.h"
#include "kfile.h"
#include "numbers.h"
#include "net/net.h"
#define MSG_PEEK_ 2
#define MSG_NOSIGNAL_ 0x4000
#define EADDRNOTAVAIL_ 99
#define ENETUNREACH_ 101
#define EALREADY_ 114
struct isock *inet_new(int type) { return isock_alloc(type == 1 ? 1 : 2); }
void inet_close(struct isock *s)
{
    if (s->type == 1) tcp_close(s);
    else isock_free(s);
    isock_wake(s);
}
static int get_in(process_t *p, uint64_t ua, uint64_t alen, uint32_t *ip, uint16_t *port)
{
    uint8_t a[16];
    if (alen < 8) return -EINVAL;
    if (copy_from_user(p, a, ua, 8) < 0) return -EFAULT;
    if (a[0] != 2 || a[1] != 0) return -EAFNOSUPPORT;
    *port = get16(a + 2);
    *ip = get32(a + 4);
    return 0;
}
int64_t inet_bind(process_t *p, struct isock *s, uint64_t uaddr, uint64_t alen)
{
    uint32_t ip; uint16_t port;
    int e = get_in(p, uaddr, alen, &ip, &port);
    if (e < 0) return e;
    if (port && port < 1024 && !perm_root(p)) return -EACCES;
    return isock_bind(s, ip, port);
}
int64_t inet_listen(struct isock *s, int backlog)
{
    if (s->type != 1) return -EOPNOTSUPP;
    if (!s->bound)
    {
        int e = isock_bind(s, 0, 0);
        if (e < 0) return e;
    }
    return tcp_listen(s, backlog);
}
int64_t inet_connect(process_t *p, struct isock *s, int nonblock, uint64_t uaddr, uint64_t alen)
{
    net_poll();
    if (s->type == 2)
    {
        uint8_t fam[2];
        if (alen >= 2 && copy_from_user(p, fam, uaddr, 2) == 0 && fam[0] == 0 && fam[1] == 0)
        {
            s->connected = 0;
            return 0;
        }
        uint32_t ip; uint16_t port;
        int e = get_in(p, uaddr, alen, &ip, &port);
        if (e < 0) return e;
        if (!s->bound && (e = isock_bind(s, 0, 0)) < 0) return e;
        s->rip = ip ? ip : 0x7F000001u; s->rport = port; s->connected = 1;
        return 0;
    }
    if (s->connecting)
    {
        if (s->state == TCP_SYN_SENT || s->state == TCP_SYN_RCVD)
            return nonblock ? -EALREADY_ : E_BLOCK;
        s->connecting = 0;
        if (s->err) { int e = s->err; s->err = 0; return -e; }
        return nonblock ? -EISCONN : 0;
    }
    if (s->state == TCP_LISTEN) return -EINVAL;
    if (s->state != TCP_CLOSED) return -EISCONN;
    uint32_t ip; uint16_t port;
    int e = get_in(p, uaddr, alen, &ip, &port);
    if (e < 0) return e;
    s->rcount = 0; s->rpos = 0; s->tcount = 0; s->tpos = 0;
    s->fin_queued = s->fin_sent = s->fin_rcvd = s->shut_rd = s->shut_wr = 0;
    s->err = 0;
    e = tcp_connect(s, ip ? ip : 0x7F000001u, port);
    if (e < 0) return e;
    s->connecting = 1;
    return nonblock ? -EINPROGRESS : E_BLOCK;
}
struct isock *inet_accept(struct isock *l)
{
    net_poll();
    return l->state == TCP_LISTEN ? tcp_accept(l) : 0;
}
int64_t inet_shutdown(struct isock *s, int how)
{
    if (s->type == 1 && (s->state == TCP_CLOSED || s->state == TCP_LISTEN || s->state == TCP_SYN_SENT))
        return -ENOTCONN;
    if (how == 0 || how == 2) s->shut_rd = 1;
    if (how == 1 || how == 2)
    {
        s->shut_wr = 1;
        if (s->type == 1) tcp_shutdown_wr(s);
    }
    isock_wake(s);
    return 0;
}
int64_t inet_send(process_t *p, struct isock *s, int nonblock, struct iov *iov, int niov, int mflags,
                  uint64_t uname, uint64_t unamelen)
{
    net_poll();
    uint64_t total = 0;
    for (int i = 0; i < niov; i++) total += iov[i].len;
    if (s->type == 2)
    {
        uint32_t ip = s->rip; uint16_t port = s->rport;
        if (uname)
        {
            int e = get_in(p, uname, unamelen, &ip, &port);
            if (e < 0) return e;
            if (!ip) ip = 0x7F000001u;
        }
        else if (!s->connected)
            return -EDESTADDRREQ;
        if (total > 1472) return -EMSGSIZE;
        uint8_t buf[1472];
        uint32_t n = 0;
        for (int i = 0; i < niov; i++)
        {
            if (iov[i].len && copy_from_user(p, buf + n, iov[i].base, iov[i].len) < 0) return -EFAULT;
            n += (uint32_t)iov[i].len;
        }
        return udp_send(s, ip, port, buf, n);
    }
    if (uname) return -EISCONN;
    if (s->err) { int e = s->err; s->err = 0; return -e; }
    if (s->state == TCP_SYN_SENT || s->state == TCP_SYN_RCVD)
        return nonblock ? -EAGAIN : E_BLOCK;
    if (s->state != TCP_ESTAB && s->state != TCP_CLOSE_WAIT)
    {
        if (s->state == TCP_CLOSED && !s->connected) return -ENOTCONN;
        if (!(mflags & MSG_NOSIGNAL_)) signal_post(p, 13);
        return -EPIPE;
    }
    if (s->shut_wr)
    {
        if (!(mflags & MSG_NOSIGNAL_)) signal_post(p, 13);
        return -EPIPE;
    }
    uint64_t done = 0;
    uint8_t tmp[1024];
    for (int i = 0; i < niov; i++)
    {
        uint64_t o = 0;
        while (o < iov[i].len)
        {
            uint32_t room = ISOCK_TX - s->tcount;
            if (!room) goto out;
            uint32_t c = room < sizeof tmp ? room : (uint32_t)sizeof tmp;
            if (c > iov[i].len - o) c = (uint32_t)(iov[i].len - o);
            if (copy_from_user(p, tmp, iov[i].base + o, c) < 0) return done ? (int64_t)done : -EFAULT;
            c = tcp_write(s, tmp, c);
            o += c; done += c;
        }
    }
out:
    if (!done && total) return nonblock ? -EAGAIN : E_BLOCK;
    return (int64_t)done;
}
int64_t inet_recv(process_t *p, struct isock *s, int nonblock, struct iov *iov, int niov, int mflags,
                  uint32_t *sip, uint16_t *sport)
{
    net_poll();
    uint64_t cap = 0;
    for (int i = 0; i < niov; i++) cap += iov[i].len;
    int peek = (mflags & MSG_PEEK_) != 0;
    if (s->type == 2)
    {
        uint8_t buf[1472];
        int32_t n = udp_recv(s, buf, cap < sizeof buf ? (uint32_t)cap : (uint32_t)sizeof buf, sip, sport, peek);
        if (n < 0)
        {
            if (s->err) { int e = s->err; s->err = 0; return -e; }
            if (s->shut_rd) return 0;
            return nonblock ? -EAGAIN : E_BLOCK;
        }
        uint32_t off = 0;
        for (int i = 0; i < niov && off < (uint32_t)n; i++)
        {
            uint32_t c = (uint32_t)iov[i].len;
            if (c > (uint32_t)n - off) c = (uint32_t)n - off;
            if (c && copy_to_user(p, iov[i].base, buf + off, c) < 0) return -EFAULT;
            off += c;
        }
        return n;
    }
    *sip = s->rip; *sport = s->rport;
    if (s->rcount == 0)
    {
        if (s->err) { int e = s->err; s->err = 0; return -e; }
        if (s->state == TCP_LISTEN) return -EINVAL;
        if (s->state == TCP_CLOSED && !s->connected) return -ENOTCONN;
        if (s->fin_rcvd || s->shut_rd || s->state == TCP_CLOSED) return 0;
        return nonblock ? -EAGAIN : E_BLOCK;
    }
    uint64_t done = 0;
    uint8_t tmp[1024];
    for (int i = 0; i < niov && s->rcount; i++)
    {
        uint64_t o = 0;
        while (o < iov[i].len && s->rcount)
        {
            uint32_t c = (uint32_t)(iov[i].len - o);
            if (c > sizeof tmp) c = sizeof tmp;
            c = tcp_read(s, tmp, c, peek);
            if (copy_to_user(p, iov[i].base + o, tmp, c) < 0) return done ? (int64_t)done : -EFAULT;
            o += c; done += c;
            if (peek) return (int64_t)done;
        }
    }
    return (int64_t)done;
}
int inet_put_name(process_t *p, uint64_t uaddr, uint32_t cap, uint32_t ip, uint16_t port, uint32_t *outlen)
{
    uint8_t a[16] = {0};
    a[0] = 2;
    put16(a + 2, port);
    put32(a + 4, ip);
    uint32_t n = cap < 16 ? cap : 16;
    *outlen = 16;
    if (n && copy_to_user(p, uaddr, a, n) < 0) return -EFAULT;
    return 0;
}
int inet_sockname(struct isock *s, int peer, uint32_t *ip, uint16_t *port)
{
    if (peer)
    {
        if (s->type == 1 ? (s->state == TCP_CLOSED || s->state == TCP_LISTEN || s->state == TCP_SYN_SENT || !s->connected)
                         : !s->connected)
            return -ENOTCONN;
        *ip = s->rip; *port = s->rport;
        return 0;
    }
    *ip = s->lip; *port = s->bound ? s->lport : 0;
    return 0;
}
int inet_getsockopt(struct isock *s, int level, int opt, uint8_t out[16], uint32_t *n)
{
    int v = 0;
    *n = 4;
    if (level == 1)
    {
        if (opt == 3) v = s->type;
        else if (opt == 4) { v = s->err; s->err = 0; }
        else if (opt == 30) v = s->state == TCP_LISTEN;
        else if (opt == 7 || opt == 8) v = ISOCK_RX;
    }
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)(v >> (8 * i));
    return 0;
}
int inet_poll(struct isock *s)
{
    net_poll();
    int r = 0;
    if (isock_readable(s)) r |= 1;
    if (isock_writable(s)) r |= 4;
    if (s->err) r |= 8;
    if (s->type == 1 && s->state == TCP_CLOSED && s->connected) r |= 0x10;
    return r;
}
int inet_nread(struct isock *s)
{
    if (s->type == 2)
    {
        if (s->rcount < 8) return 0;
        uint8_t h[8];
        rx_peek(s, h, 0, 8);
        return get16(h + 6);
    }
    return (int)s->rcount;
}
