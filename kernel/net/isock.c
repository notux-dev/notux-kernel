#include "net.h"
#include "pmm.h"
#include "process.h"
#define NISOCK 64
extern char poll_chan;
struct isock isocks[NISOCK];
struct isock *isock_alloc(int type)
{
    for (int i = 0; i < NISOCK; i++)
    {
        struct isock *s = &isocks[i];
        if (s->used) continue;
        uint8_t *z = (uint8_t *)s;
        for (unsigned k = 0; k < sizeof *s; k++) z[k] = 0;
        s->rx = pmm_alloc_contig(ISOCK_RX_PAGES);
        if (!s->rx) return 0;
        if (type == 1)
        {
            s->tx = pmm_alloc_contig(ISOCK_RX_PAGES);
            if (!s->tx) { pmm_free_contig(s->rx, ISOCK_RX_PAGES); s->rx = 0; return 0; }
        }
        s->used = 1;
        s->type = type;
        s->rto = 100;
        return s;
    }
    return 0;
}
void isock_free(struct isock *s)
{
    if (s->rx) pmm_free_contig(s->rx, ISOCK_RX_PAGES);
    if (s->tx) pmm_free_contig(s->tx, ISOCK_RX_PAGES);
    s->rx = s->tx = 0;
    s->used = 0;
    s->bound = 0;
    wakeup(s);
}
void isock_wake(struct isock *s)
{
    wakeup(s);
    wakeup(&poll_chan);
}
static int port_busy(int type, uint16_t port)
{
    for (int i = 0; i < NISOCK; i++)
    {
        struct isock *o = &isocks[i];
        if (o->used && o->bound && o->type == type && o->lport == port &&
            o->state != TCP_TIME_WAIT && !(o->type == 1 && o->state == TCP_CLOSED))
            return 1;
    }
    return 0;
}
uint16_t net_ephemeral_port(int type)
{
    static uint16_t next = 49152;
    for (int n = 0; n < 16384; n++)
    {
        uint16_t p = next;
        next = next == 65535 ? 49152 : next + 1;
        if (!port_busy(type, p)) return p;
    }
    return 0;
}
int isock_bind(struct isock *s, uint32_t ip, uint16_t port)
{
    if (s->bound) return -22;
    if (ip && ip != net_cfg.ip && ip != 0x7F000001u) return -99;
    if (port == 0)
    {
        port = net_ephemeral_port(s->type);
        if (!port) return -98;
    }
    else if (port_busy(s->type, port))
        return -98;
    s->lip = ip;
    s->lport = port;
    s->bound = 1;
    return 0;
}
int isock_readable(struct isock *s)
{
    if (s->type == 2) return s->rcount > 0 || s->err;
    if (s->state == TCP_LISTEN) return s->nacc > 0;
    return s->rcount > 0 || s->fin_rcvd || s->err || s->shut_rd ||
           s->state == TCP_CLOSED;
}
int isock_writable(struct isock *s)
{
    if (s->type == 2) return 1;
    if (s->state == TCP_ESTAB || s->state == TCP_CLOSE_WAIT) return s->tcount < ISOCK_TX;
    return s->err != 0 || s->state == TCP_CLOSED || s->shut_wr;
}
