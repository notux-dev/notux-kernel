#include "net.h"
#include "ktime.h"
extern struct isock isocks[];
#define NISOCK 64
static uint16_t udp_csum(uint32_t src, uint32_t dst, const uint8_t *p, uint32_t len)
{
    uint8_t ph[12];
    put32(ph, src); put32(ph + 4, dst); ph[8] = 0; ph[9] = IPPROTO_UDP; put16(ph + 10, (uint16_t)len);
    uint32_t sum = net_csum_add(net_csum_add(0, ph, 12), p, len);
    uint16_t c = net_csum_end(sum);
    return c ? c : 0xFFFF;
}
int udp_send_raw(uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport, const uint8_t *data, uint32_t len)
{
    if (len > 1472) return -90;
    uint8_t m[1480];
    put16(m, sport); put16(m + 2, dport); put16(m + 4, (uint16_t)(8 + len)); put16(m + 6, 0);
    net_cp(m + 8, data, len);
    put16(m + 6, udp_csum(sip, dip, m, 8 + len));
    return ip_send(sip, dip, IPPROTO_UDP, m, 8 + len) < 0 ? -101 : (int)len;
}
int udp_send(struct isock *s, uint32_t dip, uint16_t dport, const uint8_t *data, uint32_t len)
{
    if (!net_cfg.up && dip != 0xFFFFFFFFu && (dip >> 24) != 127) return -101;
    if (!s->bound)
    {
        int e = isock_bind(s, 0, 0);
        if (e < 0) return e;
    }
    uint32_t sip = s->lip ? s->lip : (dip >> 24) == 127 ? dip : net_cfg.ip;
    return udp_send_raw(sip, s->lport, dip, dport, data, len);
}
void udp_rx(uint32_t src, uint32_t dst, const uint8_t *p, uint32_t len)
{
    if (len < 8) return;
    uint16_t sport = get16(p), dport = get16(p + 2), ulen = get16(p + 4);
    if (ulen < 8 || ulen > len) return;
    if (get16(p + 6))
    {
        uint8_t ph[12];
        put32(ph, src); put32(ph + 4, dst); ph[8] = 0; ph[9] = IPPROTO_UDP; put16(ph + 10, ulen);
        if (net_csum_end(net_csum_add(net_csum_add(0, ph, 12), p, ulen)) != 0) return;
    }
    if (dport == 68 && !net_cfg.up)
    {
        dhcp_rx(p + 8, ulen - 8, net_rx_smac());
        return;
    }
    uint32_t n = ulen - 8;
    for (int i = 0; i < NISOCK; i++)
    {
        struct isock *s = &isocks[i];
        if (!s->used || s->type != 2 || !s->bound || s->lport != dport) continue;
        if (s->lip && s->lip != dst && dst != 0xFFFFFFFFu) continue;
        if (s->connected && (s->rip != src || s->rport != sport)) continue;
        if (s->shut_rd || s->rcount + 8 + n > ISOCK_RX) return;
        uint8_t h[8];
        put32(h, src); put16(h + 4, sport); put16(h + 6, (uint16_t)n);
        rx_put(s, h, 8);
        rx_put(s, p + 8, n);
        isock_wake(s);
        return;
    }
}
int32_t udp_recv(struct isock *s, uint8_t *dst, uint32_t cap, uint32_t *sip, uint16_t *sport, int peek)
{
    if (s->rcount < 8) return -1;
    uint8_t h[8];
    rx_peek(s, h, 0, 8);
    uint32_t n = get16(h + 6), take = n < cap ? n : cap;
    *sip = get32(h); *sport = get16(h + 4);
    rx_peek(s, dst, 8, take);
    if (!peek) rx_drop(s, 8 + n);
    return (int32_t)take;
}
