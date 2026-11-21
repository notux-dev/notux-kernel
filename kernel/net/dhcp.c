#include "net.h"
#include "ktime.h"
#include "serial.h"
static int state;
static uint32_t xid, offer_ip, server_ip;
static uint64_t retry_at;
static int tries;
static void say_ip(const char *what, uint32_t ip)
{
    char b[48]; int n = 0;
    for (const char *w = what; *w; w++) b[n++] = *w;
    for (int i = 3; i >= 0; i--)
    {
        unsigned v = (ip >> (8 * i)) & 255;
        if (v >= 100) b[n++] = (char)('0' + v / 100);
        if (v >= 10) b[n++] = (char)('0' + v / 10 % 10);
        b[n++] = (char)('0' + v % 10);
        if (i) b[n++] = '.';
    }
    b[n++] = '\n'; b[n] = 0;
    serwrite(b);
}
static void build_and_send(int type)
{
    uint8_t m[300] = {0};
    m[0] = 1; m[1] = 1; m[2] = 6;
    put32(m + 4, xid); put16(m + 10, 0x8000);
    net_cp(m + 28, e1000_mac(), 6);
    m[236] = 99; m[237] = 130; m[238] = 83; m[239] = 99;
    uint8_t *o = m + 240;
    *o++ = 53; *o++ = 1; *o++ = (uint8_t)type;
    if (type == 3)
    {
        *o++ = 50; *o++ = 4; put32(o, offer_ip); o += 4;
        *o++ = 54; *o++ = 4; put32(o, server_ip); o += 4;
    }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
    *o++ = 255;
    udp_send_raw(0, 68, 0xFFFFFFFFu, 67, m, (uint32_t)(o - m));
}
void dhcp_start(void)
{
    const uint8_t *mac = e1000_mac();
    xid = 0x4e580000u | (uint32_t)(mac[4] << 8) | mac[5];
    state = 1; tries = 0;
    retry_at = ktime_ticks + 200;
    build_and_send(1);
}
static void configure(uint32_t ip, uint32_t mask, uint32_t gw, uint32_t dns)
{
    net_cfg.ip = ip; net_cfg.mask = mask; net_cfg.gw = gw; net_cfg.dns = dns;
    net_cfg.up = 1;
    state = 3;
}
void dhcp_tick(void)
{
    if (state == 0 || state == 3 || ktime_ticks < retry_at) return;
    retry_at = ktime_ticks + 200;
    if (++tries >= 3)
    {
        serwrite("NET: DHCP gave no answer, using 10.0.2.15/24\n");
        configure(IP4(10, 0, 2, 15), IP4(255, 255, 255, 0), IP4(10, 0, 2, 2), IP4(10, 0, 2, 3));
        return;
    }
    build_and_send(state == 2 ? 3 : 1);
}
int dhcp_rx(const uint8_t *p, uint32_t len, const uint8_t *smac)
{
    if (state != 1 && state != 2) return 0;
    if (len < 241 || p[0] != 2 || get32(p + 4) != xid || get32(p + 236) != 0x63825363u) return 0;
    uint32_t yiaddr = get32(p + 16), mask = IP4(255, 255, 255, 0), gw = 0, dns = 0, srv = get32(p + 20);
    int type = 0;
    for (uint32_t i = 240; i + 1 < len && p[i] != 255;)
    {
        if (p[i] == 0) { i++; continue; }
        uint32_t l = p[i + 1];
        if (i + 2 + l > len) break;
        const uint8_t *v = p + i + 2;
        if (p[i] == 53 && l == 1) type = v[0];
        else if (p[i] == 1 && l == 4) mask = get32(v);
        else if (p[i] == 3 && l >= 4) gw = get32(v);
        else if (p[i] == 6 && l >= 4) dns = get32(v);
        else if (p[i] == 54 && l == 4) srv = get32(v);
        i += 2 + l;
    }
    if (type == 2 && state == 1)
    {
        offer_ip = yiaddr; server_ip = srv;
        state = 2; tries = 0; retry_at = ktime_ticks + 200;
        build_and_send(3);
    }
    else if (type == 5)
    {
        configure(yiaddr, mask, gw, dns);
        net_arp_learn(srv, smac);
        say_ip("NET: DHCP address ", yiaddr);
        say_ip("NET: gateway ", gw);
        say_ip("NET: dns ", dns);
    }
    else if (type == 6)
        dhcp_start();
    return 1;
}
