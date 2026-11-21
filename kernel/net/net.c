#include "net.h"
#include "ktime.h"
#include "serial.h"
struct ipcfg net_cfg;
volatile uint32_t net_ping_replies;
static int nic;
static const uint8_t bcast[6] = {255, 255, 255, 255, 255, 255};
static const uint8_t *rx_smac;
uint32_t net_csum_add(uint32_t sum, const uint8_t *p, uint32_t n)
{
    for (; n > 1; n -= 2, p += 2) sum += get16(p);
    if (n) sum += (uint32_t)p[0] << 8;
    return sum;
}
uint16_t net_csum_end(uint32_t sum)
{
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}
#define NARP 8
static struct { uint32_t ip; uint8_t mac[6]; } arp[NARP];
static int arp_next;
static struct { uint32_t nexthop; uint32_t len; uint8_t frame[1514]; } pend;
static const uint8_t *arp_lookup(uint32_t ip)
{
    for (int i = 0; i < NARP; i++)
        if (arp[i].ip == ip) return arp[i].mac;
    return 0;
}
static void arp_learn(uint32_t ip, const uint8_t *mac)
{
    if (!ip) return;
    int slot = -1;
    for (int i = 0; i < NARP; i++)
        if (arp[i].ip == ip) slot = i;
    if (slot < 0) { slot = arp_next; arp_next = (arp_next + 1) % NARP; }
    arp[slot].ip = ip;
    net_cp(arp[slot].mac, mac, 6);
    if (pend.len && pend.nexthop == ip)
    {
        net_cp(pend.frame, mac, 6);
        e1000_send(pend.frame, pend.len);
        pend.len = 0;
    }
}
static void arp_send(uint16_t op, const uint8_t *tmac, uint32_t tip)
{
    uint8_t f[60] = {0};
    net_cp(f, op == 1 ? bcast : tmac, 6); net_cp(f + 6, e1000_mac(), 6); put16(f + 12, 0x0806);
    put16(f + 14, 1); put16(f + 16, 0x0800); f[18] = 6; f[19] = 4; put16(f + 20, op);
    net_cp(f + 22, e1000_mac(), 6); put32(f + 28, net_cfg.ip);
    if (op == 2) net_cp(f + 32, tmac, 6);
    put32(f + 38, tip);
    e1000_send(f, 60);
}
#define NLO 16
static struct { uint32_t len; uint8_t pkt[1500]; } lo[NLO];
static int lo_head, lo_n;
static int is_local(uint32_t dst) { return (dst >> 24) == 127 || (net_cfg.up && dst == net_cfg.ip); }
static void ip_input(const uint8_t *ih, uint32_t len, const uint8_t *smac);
static void lo_drain(void)
{
    while (lo_n)
    {
        int i = lo_head;
        lo_head = (lo_head + 1) % NLO;
        lo_n--;
        ip_input(lo[i].pkt, lo[i].len, 0);
    }
}
int ip_send(uint32_t src, uint32_t dst, uint8_t proto, const uint8_t *data, uint32_t len)
{
    static uint16_t ident;
    if ((!nic && !is_local(dst)) || len > 1480) return -1;
    static uint8_t f[1514];
    uint8_t *ih = f + 14;
    ih[0] = 0x45; ih[1] = 0; put16(ih + 2, (uint16_t)(20 + len)); put16(ih + 4, ++ident);
    put16(ih + 6, 0x4000); ih[8] = 64; ih[9] = proto; put16(ih + 10, 0);
    put32(ih + 12, src); put32(ih + 16, dst);
    put16(ih + 10, net_csum_end(net_csum_add(0, ih, 20)));
    net_cp(ih + 20, data, len);
    if (is_local(dst))
    {
        if (lo_n == NLO) return -1;
        int i = (lo_head + lo_n++) % NLO;
        lo[i].len = 20 + len;
        net_cp(lo[i].pkt, ih, 20 + len);
        return 0;
    }
    net_cp(f + 6, e1000_mac(), 6); put16(f + 12, 0x0800);
    uint32_t total = 14 + 20 + len;
    uint32_t hop;
    if (dst == 0xFFFFFFFFu || (net_cfg.up && (dst | ~net_cfg.mask) == 0xFFFFFFFFu))
    {
        net_cp(f, bcast, 6);
        return e1000_send(f, total);
    }
    hop = (net_cfg.up && (dst & net_cfg.mask) != (net_cfg.ip & net_cfg.mask)) ? net_cfg.gw : dst;
    if (!hop) return -1;
    const uint8_t *mac = arp_lookup(hop);
    if (mac)
    {
        net_cp(f, mac, 6);
        return e1000_send(f, total);
    }
    net_cp(pend.frame, f, total);
    pend.len = total; pend.nexthop = hop;
    arp_send(1, 0, hop);
    return 0;
}
static void icmp_rx(uint32_t src, uint32_t dst, const uint8_t *p, uint32_t len)
{
    if (len < 8 || net_csum_end(net_csum_add(0, p, len)) != 0) return;
    if (p[0] == 8 && len <= 1400)
    {
        uint8_t r[1400];
        net_cp(r, p, len);
        r[0] = 0; put16(r + 2, 0);
        put16(r + 2, net_csum_end(net_csum_add(0, r, len)));
        ip_send(dst, src, IPPROTO_ICMP, r, len);
    }
    else if (p[0] == 0)
        net_ping_replies++;
}
int net_ping(uint32_t ip)
{
    static const uint8_t payload[32] = "notux: hello over e1000 ........";
    static uint16_t seq;
    uint8_t m[8 + sizeof payload] = {0};
    m[0] = 8; put16(m + 4, 0x4e58); put16(m + 6, ++seq);
    net_cp(m + 8, payload, sizeof payload);
    put16(m + 2, net_csum_end(net_csum_add(0, m, sizeof m)));
    return ip_send(net_cfg.ip, ip, IPPROTO_ICMP, m, sizeof m);
}
static void ip_input(const uint8_t *ih, uint32_t len, const uint8_t *smac)
{
    if (len < 20 || (ih[0] >> 4) != 4) return;
    uint32_t hl = (uint32_t)(ih[0] & 15) * 4, tot = get16(ih + 2);
    if (hl < 20 || tot < hl || tot > len) return;
    if (net_csum_end(net_csum_add(0, ih, hl)) != 0) return;
    if (get16(ih + 6) & 0x3FFF) return;
    uint32_t src = get32(ih + 12), dst = get32(ih + 16);
    int mine = dst == net_cfg.ip || dst == 0xFFFFFFFFu || (dst >> 24) == 127 ||
               (net_cfg.up && (dst | ~net_cfg.mask) == 0xFFFFFFFFu) || !net_cfg.up;
    if (!mine) return;
    const uint8_t *p = ih + hl;
    uint32_t n = tot - hl;
    if (smac && net_cfg.up && ((src & net_cfg.mask) == (net_cfg.ip & net_cfg.mask)))
        arp_learn(src, smac);
    switch (ih[9])
    {
    case IPPROTO_ICMP: if (dst == net_cfg.ip || (dst >> 24) == 127) icmp_rx(src, dst, p, n); break;
    case IPPROTO_UDP: udp_rx(src, dst, p, n); break;
    case IPPROTO_TCP: if (dst == net_cfg.ip || (dst >> 24) == 127) tcp_rx(src, dst, p, n); break;
    }
}
static void eth_rx(const uint8_t *f, uint32_t len)
{
    if (len < 14) return;
    rx_smac = f + 6;
    uint16_t type = get16(f + 12);
    if (type == 0x0806 && len >= 42 && get16(f + 20) <= 2)
    {
        uint32_t sip = get32(f + 28), tip = get32(f + 38);
        if (net_cfg.up) arp_learn(sip, f + 22);
        if (get16(f + 20) == 1 && net_cfg.up && tip == net_cfg.ip)
            arp_send(2, f + 22, sip);
    }
    else if (type == 0x0800)
        ip_input(f + 14, len - 14, f + 6);
}
const uint8_t *net_rx_smac(void) { return rx_smac; }
void net_arp_learn(uint32_t ip, const uint8_t *mac) { arp_learn(ip, mac); }
void net_poll(void)
{
    lo_drain();
    if (nic) e1000_poll(eth_rx);
    lo_drain();
}
void net_init(void)
{
    nic = e1000_init();
    serwrite(nic ? "NET: e1000 found\n" : "NET: no e1000\n");
    if (nic) dhcp_start();
}
void net_tick(void)
{
    static int reported, tries;
    static uint64_t next;
    net_poll();
    if (!nic) { tcp_tick(); return; }
    dhcp_tick();
    tcp_tick();
    if (reported || !net_cfg.up) return;
    if (net_ping_replies)
    {
        serwrite("NET: ping gateway ok\n");
        reported = 1;
    }
    else if (tries < 5 && ktime_ticks >= next)
    {
        tries++;
        next = ktime_ticks + 100;
        net_ping(net_cfg.gw);
    }
}
