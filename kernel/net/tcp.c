#include "net.h"
#include "ktime.h"
extern struct isock isocks[];
#define NISOCK 64
#define MSS 1460
#define F_FIN 1
#define F_SYN 2
#define F_RST 4
#define F_PSH 8
#define F_ACK 16
#define SEQ_LT(a,b) ((int32_t)((a) - (b)) < 0)
#define SEQ_LE(a,b) ((int32_t)((a) - (b)) <= 0)
#define ETIMEDOUT_ 110
#define ECONNREFUSED_ 111
#define ECONNRESET_ 104
#define TW_TICKS 200
#define FW2_TICKS 3000
static uint32_t new_iss(void)
{
    static uint32_t c;
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return lo ^ (c += 64007);
}
static uint32_t wnd_of(struct isock *s)
{
    uint32_t w = ISOCK_RX - s->rcount;
    return w > 65535 ? 65535 : w;
}
static uint32_t pick(uint32_t a, uint32_t b) { return a < b ? a : b; }
static void seg_raw(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack,
                    uint8_t flags, uint32_t wnd, const uint8_t *opt, uint32_t optlen,
                    struct isock *data_from, uint32_t off, uint32_t n)
{
    uint8_t m[24 + MSS];
    uint32_t hl = 20 + optlen;
    put16(m, sport); put16(m + 2, dport); put32(m + 4, seq); put32(m + 8, ack);
    m[12] = (uint8_t)((hl / 4) << 4); m[13] = flags; put16(m + 14, (uint16_t)wnd);
    put16(m + 16, 0); put16(m + 18, 0);
    for (uint32_t i = 0; i < optlen; i++) m[20 + i] = opt[i];
    for (uint32_t i = 0; i < n; i++) m[hl + i] = data_from->tx[(data_from->tpos + off + i) % ISOCK_TX];
    uint8_t ph[12];
    put32(ph, sip); put32(ph + 4, dip); ph[8] = 0; ph[9] = IPPROTO_TCP; put16(ph + 10, (uint16_t)(hl + n));
    put16(m + 16, net_csum_end(net_csum_add(net_csum_add(0, ph, 12), m, hl + n)));
    ip_send(sip, dip, IPPROTO_TCP, m, hl + n);
}
static void seg_out(struct isock *s, uint8_t flags, uint32_t seq, uint32_t off, uint32_t n)
{
    static const uint8_t mssopt[4] = {2, 4, MSS >> 8, MSS & 255};
    seg_raw(s->lip, s->rip, s->lport, s->rport, seq, (flags & F_ACK) ? s->rcv_nxt : 0, flags,
            wnd_of(s), (flags & F_SYN) ? mssopt : 0, (flags & F_SYN) ? 4 : 0, s, off, n);
}
static void send_ack(struct isock *s) { seg_out(s, F_ACK, s->snd_nxt, 0, 0); }
static void rst_for(uint32_t src, uint32_t dst, const uint8_t *p, uint32_t dlen)
{
    uint8_t fl = p[13];
    if (fl & F_RST) return;
    uint32_t seq = get32(p + 4), ack = get32(p + 8);
    if (fl & F_ACK)
        seg_raw(dst, src, get16(p + 2), get16(p), ack, 0, F_RST, 0, 0, 0, 0, 0, 0);
    else
        seg_raw(dst, src, get16(p + 2), get16(p), 0, seq + dlen + ((fl & F_SYN) ? 1 : 0) + ((fl & F_FIN) ? 1 : 0),
                F_RST | F_ACK, 0, 0, 0, 0, 0, 0);
}
static void arm(struct isock *s)
{
    if (!s->timer_on) { s->timer_on = 1; s->rto_at = ktime_ticks + s->rto; }
}
static void restart(struct isock *s) { s->timer_on = 1; s->rto_at = ktime_ticks + s->rto; }
static void gone(struct isock *s)
{
    s->state = TCP_CLOSED;
    s->timer_on = 0;
    if (s->orphan) isock_free(s);
    else isock_wake(s);
}
static void output(struct isock *s, int force)
{
    int st = s->state;
    if (st != TCP_ESTAB && st != TCP_CLOSE_WAIT && st != TCP_FIN_WAIT1 && st != TCP_CLOSING && st != TCP_LAST_ACK)
        return;
    uint32_t sent = s->snd_nxt - s->snd_una - (s->fin_sent ? 1 : 0);
    uint32_t unsent = s->tcount - sent;
    uint32_t mss = s->mss ? pick(s->mss, MSS) : 536;
    while (unsent)
    {
        uint32_t inflight = s->snd_nxt - s->snd_una;
        uint32_t room = s->snd_wnd > inflight ? s->snd_wnd - inflight : 0;
        if (!room && force) room = 1;
        if (!room) break;
        uint32_t n = pick(pick(unsent, room), mss);
        seg_out(s, F_ACK | F_PSH, s->snd_nxt, sent, n);
        s->snd_nxt += n; sent += n; unsent -= n; force = 0;
        arm(s);
    }
    if (s->fin_queued && !s->fin_sent && !unsent)
    {
        seg_out(s, F_FIN | F_ACK, s->snd_nxt, 0, 0);
        s->snd_nxt++; s->fin_sent = 1;
        arm(s);
    }
}
static int ack_in(struct isock *s, uint32_t ack, uint32_t wnd)
{
    int fin_acked = 0;
    if (SEQ_LT(s->snd_una, ack) && SEQ_LE(ack, s->snd_nxt))
    {
        uint32_t acked = ack - s->snd_una;
        if (s->fin_sent && ack == s->snd_nxt) { acked--; fin_acked = 1; }
        if (acked > s->tcount) acked = s->tcount;
        s->tpos = (s->tpos + acked) % ISOCK_TX;
        s->tcount -= acked;
        s->snd_una = ack;
        s->retries = 0; s->rto = 100;
        if (s->snd_una == s->snd_nxt) s->timer_on = 0; else restart(s);
        isock_wake(s);
    }
    if (SEQ_LE(s->snd_una, ack)) s->snd_wnd = wnd;
    return fin_acked;
}
static void parse_mss(struct isock *s, const uint8_t *p, uint32_t doff)
{
    s->mss = 536;
    for (uint32_t i = 20; i < doff;)
    {
        if (p[i] == 0) break;
        if (p[i] == 1) { i++; continue; }
        if (i + 1 >= doff || p[i + 1] < 2) break;
        if (p[i] == 2 && p[i + 1] == 4 && i + 3 < doff) s->mss = get16(p + i + 2);
        i += p[i + 1];
    }
}
static struct isock *find_conn(uint32_t src, uint32_t dst, uint16_t sport, uint16_t dport)
{
    struct isock *listener = 0;
    for (int i = 0; i < NISOCK; i++)
    {
        struct isock *s = &isocks[i];
        if (!s->used || s->type != 1 || !s->bound || s->lport != dport) continue;
        if (s->state == TCP_LISTEN)
        {
            if (!s->lip || s->lip == dst) listener = s;
        }
        else if (s->state != TCP_CLOSED && s->rip == src && s->rport == sport)
            return s;
    }
    return listener;
}
void tcp_rx(uint32_t src, uint32_t dst, const uint8_t *p, uint32_t len)
{
    if (len < 20) return;
    uint8_t ph[12];
    put32(ph, src); put32(ph + 4, dst); ph[8] = 0; ph[9] = IPPROTO_TCP; put16(ph + 10, (uint16_t)len);
    if (net_csum_end(net_csum_add(net_csum_add(0, ph, 12), p, len)) != 0) return;
    uint16_t sport = get16(p), dport = get16(p + 2), wnd = get16(p + 14);
    uint32_t seq = get32(p + 4), ack = get32(p + 8), doff = (uint32_t)(p[12] >> 4) * 4;
    uint8_t fl = p[13];
    if (doff < 20 || doff > len) return;
    const uint8_t *data = p + doff;
    uint32_t dlen = len - doff;
    struct isock *s = find_conn(src, dst, sport, dport);
    if (!s) { rst_for(src, dst, p, dlen); return; }
    if (s->state == TCP_LISTEN)
    {
        if (fl & F_RST) return;
        if (!(fl & F_SYN) || (fl & F_ACK)) { rst_for(src, dst, p, dlen); return; }
        uint32_t pending = (uint32_t)s->nacc;
        for (int i = 0; i < NISOCK; i++)
            if (isocks[i].used && isocks[i].parent == s && isocks[i].state == TCP_SYN_RCVD) pending++;
        if (pending >= (uint32_t)s->backlog) return;
        struct isock *c = isock_alloc(1);
        if (!c) return;
        c->lip = dst; c->lport = dport; c->rip = src; c->rport = sport;
        c->bound = 1; c->connected = 1; c->parent = s;
        c->irs = seq; c->rcv_nxt = seq + 1;
        c->iss = new_iss(); c->snd_una = c->iss; c->snd_nxt = c->iss + 1;
        c->snd_wnd = wnd;
        parse_mss(c, p, doff);
        c->state = TCP_SYN_RCVD;
        seg_out(c, F_SYN | F_ACK, c->iss, 0, 0);
        restart(c);
        return;
    }
    if (s->state == TCP_SYN_SENT)
    {
        if ((fl & F_ACK) && ack != s->iss + 1)
        {
            if (!(fl & F_RST))
                seg_raw(dst, src, dport, sport, ack, 0, F_RST, 0, 0, 0, 0, 0, 0);
            return;
        }
        if (fl & F_RST)
        {
            if (fl & F_ACK) { s->err = ECONNREFUSED_; gone(s); }
            return;
        }
        if ((fl & (F_SYN | F_ACK)) == (F_SYN | F_ACK))
        {
            s->irs = seq; s->rcv_nxt = seq + 1;
            s->snd_una = s->snd_nxt = ack;
            s->snd_wnd = wnd;
            parse_mss(s, p, doff);
            s->state = TCP_ESTAB;
            s->timer_on = 0; s->retries = 0; s->rto = 100;
            send_ack(s);
            isock_wake(s);
        }
        return;
    }
    if (s->state == TCP_SYN_RCVD)
    {
        if (fl & F_RST) { s->state = TCP_CLOSED; isock_free(s); return; }
        if ((fl & F_SYN) && seq == s->irs) { seg_out(s, F_SYN | F_ACK, s->iss, 0, 0); return; }
        if (!(fl & F_ACK) || ack != s->iss + 1) { rst_for(src, dst, p, dlen); return; }
        struct isock *l = s->parent;
        if (!l || !l->used || l->state != TCP_LISTEN || l->nacc >= ISOCK_BACKLOG)
        {
            rst_for(src, dst, p, dlen);
            s->state = TCP_CLOSED; isock_free(s);
            return;
        }
        s->snd_una = s->snd_nxt = ack;
        s->snd_wnd = wnd;
        s->state = TCP_ESTAB;
        s->timer_on = 0; s->retries = 0; s->rto = 100;
        l->accq[l->nacc++] = s;
        isock_wake(l);
    }
    if (fl & F_RST)
    {
        if (seq != s->rcv_nxt && s->state != TCP_TIME_WAIT) return;
        s->err = s->state == TCP_CLOSE_WAIT || s->state == TCP_LAST_ACK ? 32 : ECONNRESET_;
        gone(s);
        return;
    }
    if (fl & F_SYN) { send_ack(s); return; }
    if (!(fl & F_ACK)) return;
    int fin_acked = ack_in(s, ack, wnd);
    if (fin_acked)
    {
        if (s->state == TCP_FIN_WAIT1) { s->state = TCP_FIN_WAIT2; s->dead_at = ktime_ticks + FW2_TICKS; }
        else if (s->state == TCP_CLOSING) { s->state = TCP_TIME_WAIT; s->dead_at = ktime_ticks + TW_TICKS; }
        else if (s->state == TCP_LAST_ACK) { gone(s); return; }
    }
    int rx_ok = s->state == TCP_ESTAB || s->state == TCP_FIN_WAIT1 || s->state == TCP_FIN_WAIT2;
    if (rx_ok && (dlen || (fl & F_FIN)))
    {
        if (SEQ_LT(seq, s->rcv_nxt))
        {
            uint32_t skip = s->rcv_nxt - seq;
            if (skip > dlen || (skip == dlen && !(fl & F_FIN))) { send_ack(s); return; }
            if (skip > dlen) skip = dlen;
            data += skip; dlen -= skip; seq = s->rcv_nxt;
        }
        if (seq != s->rcv_nxt) { send_ack(s); return; }
        uint32_t n = dlen;
        if (!s->orphan)
        {
            uint32_t space = ISOCK_RX - s->rcount;
            if (n > space) { n = space; fl &= (uint8_t)~F_FIN; }
            if (n) rx_put(s, data, n);
        }
        s->rcv_nxt += n;
        if ((fl & F_FIN) && n == dlen)
        {
            s->rcv_nxt++;
            s->fin_rcvd = 1;
            if (s->state == TCP_ESTAB) s->state = TCP_CLOSE_WAIT;
            else if (s->state == TCP_FIN_WAIT1) s->state = TCP_CLOSING;
            else if (s->state == TCP_FIN_WAIT2) { s->state = TCP_TIME_WAIT; s->dead_at = ktime_ticks + TW_TICKS; }
        }
        send_ack(s);
        isock_wake(s);
    }
    else if (dlen || (fl & F_FIN))
        send_ack(s);
    if (s->used) output(s, 0);
}
void tcp_tick(void)
{
    for (int i = 0; i < NISOCK; i++)
    {
        struct isock *s = &isocks[i];
        if (!s->used || s->type != 1) continue;
        if ((s->state == TCP_TIME_WAIT || s->state == TCP_FIN_WAIT2) && ktime_ticks >= s->dead_at)
        {
            gone(s);
            continue;
        }
        if (!s->timer_on || ktime_ticks < s->rto_at) continue;
        if (++s->retries > 8)
        {
            s->err = ETIMEDOUT_;
            gone(s);
            continue;
        }
        if (s->state == TCP_SYN_SENT) seg_out(s, F_SYN, s->iss, 0, 0);
        else if (s->state == TCP_SYN_RCVD) seg_out(s, F_SYN | F_ACK, s->iss, 0, 0);
        else
        {
            s->snd_nxt = s->snd_una;
            s->fin_sent = 0;
            output(s, 1);
        }
        s->rto = s->rto * 2 > 6000 ? 6000 : s->rto * 2;
        restart(s);
    }
}
int tcp_connect(struct isock *s, uint32_t ip, uint16_t port)
{
    if (!net_cfg.up && (ip >> 24) != 127) return -101;
    if (!s->bound)
    {
        int e = isock_bind(s, 0, 0);
        if (e < 0) return e;
    }
    s->lip = (ip >> 24) == 127 ? ip : net_cfg.ip;
    s->rip = ip; s->rport = port; s->connected = 1;
    s->iss = new_iss(); s->snd_una = s->iss; s->snd_nxt = s->iss + 1;
    s->state = TCP_SYN_SENT;
    s->rto = 100; s->retries = 0;
    seg_out(s, F_SYN, s->iss, 0, 0);
    restart(s);
    return 0;
}
int tcp_listen(struct isock *s, int backlog)
{
    if (!s->bound) return -22;
    s->backlog = backlog < 1 ? 1 : backlog > ISOCK_BACKLOG ? ISOCK_BACKLOG : backlog;
    s->state = TCP_LISTEN;
    return 0;
}
struct isock *tcp_accept(struct isock *l)
{
    if (!l->nacc) return 0;
    struct isock *c = l->accq[0];
    for (int i = 1; i < l->nacc; i++) l->accq[i - 1] = l->accq[i];
    l->nacc--;
    c->parent = 0;
    return c;
}
uint32_t tcp_write(struct isock *s, const uint8_t *src, uint32_t n)
{
    if (s->state != TCP_ESTAB && s->state != TCP_CLOSE_WAIT) return 0;
    uint32_t room = ISOCK_TX - s->tcount;
    if (n > room) n = room;
    uint32_t w = (s->tpos + s->tcount) % ISOCK_TX;
    for (uint32_t i = 0; i < n; i++) { s->tx[w] = src[i]; if (++w == ISOCK_TX) w = 0; }
    s->tcount += n;
    output(s, 0);
    return n;
}
uint32_t tcp_read(struct isock *s, uint8_t *dst, uint32_t n, int peek)
{
    if (n > s->rcount) n = s->rcount;
    uint32_t before = wnd_of(s);
    rx_peek(s, dst, 0, n);
    if (!peek)
    {
        rx_drop(s, n);
        if (before < MSS && wnd_of(s) >= MSS &&
            (s->state == TCP_ESTAB || s->state == TCP_FIN_WAIT1 || s->state == TCP_FIN_WAIT2))
            send_ack(s);
    }
    return n;
}
void tcp_shutdown_wr(struct isock *s)
{
    if (s->fin_queued) return;
    if (s->state == TCP_ESTAB) s->state = TCP_FIN_WAIT1;
    else if (s->state == TCP_CLOSE_WAIT) s->state = TCP_LAST_ACK;
    else return;
    s->fin_queued = 1;
    output(s, 0);
}
void tcp_close(struct isock *s)
{
    switch (s->state)
    {
    case TCP_LISTEN:
        for (int i = 0; i < s->nacc; i++)
        {
            struct isock *c = s->accq[i];
            seg_out(c, F_RST | F_ACK, c->snd_nxt, 0, 0);
            c->state = TCP_CLOSED; isock_free(c);
        }
        for (int i = 0; i < NISOCK; i++)
            if (isocks[i].used && isocks[i].parent == s)
            {
                isocks[i].state = TCP_CLOSED; isock_free(&isocks[i]);
            }
        s->state = TCP_CLOSED; isock_free(s);
        break;
    case TCP_CLOSED: case TCP_SYN_SENT:
        s->state = TCP_CLOSED; isock_free(s);
        break;
    case TCP_ESTAB: case TCP_CLOSE_WAIT:
        s->orphan = 1;
        tcp_shutdown_wr(s);
        break;
    default:
        s->orphan = 1;
        break;
    }
}
