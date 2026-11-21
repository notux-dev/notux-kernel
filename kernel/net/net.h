#ifndef NET_H
#define NET_H 
#include "stdint.h"
int e1000_init(void);
int e1000_send(const void *frame, uint32_t len);
void e1000_poll(void (*rx)(const uint8_t *frame, uint32_t len));
const uint8_t *e1000_mac(void);
int e1000_irq_line(void);
void e1000_irq(void);
#define IP4(a,b,c,d) (((uint32_t)(a) << 24) | ((b) << 16) | ((c) << 8) | (d))
static inline void net_cp(void *d, const void *s, uint32_t n)
{
    asm volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
}
static inline void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }
static inline uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) << 16 | get16(p + 2); }
uint32_t net_csum_add(uint32_t sum, const uint8_t *p, uint32_t n);
uint16_t net_csum_end(uint32_t sum);
struct ipcfg { uint32_t ip, mask, gw, dns; int up; };
extern struct ipcfg net_cfg;
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
void net_init(void);
void net_tick(void);
void net_poll(void);
int ip_send(uint32_t src, uint32_t dst, uint8_t proto, const uint8_t *data, uint32_t len);
int net_ping(uint32_t ip);
const uint8_t *net_rx_smac(void);
void net_arp_learn(uint32_t ip, const uint8_t *mac);
extern volatile uint32_t net_ping_replies;
#define ISOCK_RX_PAGES 15
#define ISOCK_RX (ISOCK_RX_PAGES * 4096)
#define ISOCK_TX (ISOCK_RX_PAGES * 4096)
#define ISOCK_BACKLOG 8
enum { TCP_CLOSED, TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RCVD, TCP_ESTAB, TCP_FIN_WAIT1, TCP_FIN_WAIT2,
       TCP_CLOSE_WAIT, TCP_CLOSING, TCP_LAST_ACK, TCP_TIME_WAIT };
struct isock {
    int used, type;
    int state;
    uint32_t lip, rip;
    uint16_t lport, rport;
    int bound, connected, connecting, orphan, shut_rd, shut_wr;
    int err;
    uint8_t *rx; uint32_t rpos, rcount;
    uint8_t *tx; uint32_t tpos, tcount;
    uint32_t iss, irs, snd_una, snd_nxt, snd_wnd, rcv_nxt;
    int fin_queued, fin_sent, fin_rcvd;
    uint32_t mss;
    uint64_t rto_at; uint32_t rto; int retries, timer_on;
    uint64_t dead_at;
    struct isock *accq[ISOCK_BACKLOG]; int nacc, backlog;
    struct isock *parent;
};
static inline void rx_put(struct isock *s, const uint8_t *src, uint32_t n)
{
    uint32_t w = (s->rpos + s->rcount) % ISOCK_RX;
    for (uint32_t i = 0; i < n; i++) { s->rx[w] = src[i]; if (++w == ISOCK_RX) w = 0; }
    s->rcount += n;
}
static inline void rx_peek(struct isock *s, uint8_t *dst, uint32_t off, uint32_t n)
{
    uint32_t r = (s->rpos + off) % ISOCK_RX;
    for (uint32_t i = 0; i < n; i++) { dst[i] = s->rx[r]; if (++r == ISOCK_RX) r = 0; }
}
static inline void rx_drop(struct isock *s, uint32_t n)
{
    s->rpos = (s->rpos + n) % ISOCK_RX;
    s->rcount -= n;
}
struct isock *isock_alloc(int type);
void isock_free(struct isock *s);
int isock_bind(struct isock *s, uint32_t ip, uint16_t port);
uint16_t net_ephemeral_port(int type);
int isock_readable(struct isock *s);
int isock_writable(struct isock *s);
void isock_wake(struct isock *s);
void udp_rx(uint32_t src, uint32_t dst, const uint8_t *p, uint32_t len);
int udp_send(struct isock *s, uint32_t dip, uint16_t dport, const uint8_t *data, uint32_t len);
int udp_send_raw(uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport, const uint8_t *data, uint32_t len);
int32_t udp_recv(struct isock *s, uint8_t *dst, uint32_t cap, uint32_t *sip, uint16_t *sport, int peek);
void dhcp_start(void);
void dhcp_tick(void);
int dhcp_rx(const uint8_t *p, uint32_t len, const uint8_t *smac);
void tcp_rx(uint32_t src, uint32_t dst, const uint8_t *p, uint32_t len);
void tcp_tick(void);
int tcp_connect(struct isock *s, uint32_t ip, uint16_t port);
int tcp_listen(struct isock *s, int backlog);
struct isock *tcp_accept(struct isock *l);
uint32_t tcp_write(struct isock *s, const uint8_t *src, uint32_t n);
uint32_t tcp_read(struct isock *s, uint8_t *dst, uint32_t n, int peek);
void tcp_shutdown_wr(struct isock *s);
void tcp_close(struct isock *s);
#endif
