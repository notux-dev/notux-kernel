#include "net.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include "pic.h"
#define R_CTRL 0x0000
#define R_STATUS 0x0008
#define R_ICR 0x00C0
#define R_IMS 0x00D0
#define R_IMC 0x00D8
#define R_RCTL 0x0100
#define R_TCTL 0x0400
#define R_TIPG 0x0410
#define R_RDBAL 0x2800
#define R_RDBAH 0x2804
#define R_RDLEN 0x2808
#define R_RDH 0x2810
#define R_RDT 0x2818
#define R_TDBAL 0x3800
#define R_TDBAH 0x3804
#define R_TDLEN 0x3808
#define R_TDH 0x3810
#define R_TDT 0x3818
#define R_MTA 0x5200
#define R_RAL0 0x5400
#define R_RAH0 0x5404
#define NRX 32
#define NTX 8
#define BUFSZ 2048
struct rxd { uint64_t addr; uint16_t len, csum; uint8_t status, err; uint16_t special; };
struct txd { uint64_t addr; uint16_t len; uint8_t cso, cmd, status, css; uint16_t special; };
static volatile uint8_t *mmio;
static struct rxd *rx;
static struct txd *tx;
static uint8_t *rxbuf, *txbuf;
static uint32_t rx_next, tx_next;
static uint8_t mac[6];
static int irq_line = -1;
static uint32_t rd(uint32_t r) { return *(volatile uint32_t *)(mmio + r); }
static void wr(uint32_t r, uint32_t v) { *(volatile uint32_t *)(mmio + r) = v; }
static void outl(uint16_t p, uint32_t v) { asm volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static uint32_t inl(uint16_t p) { uint32_t v; asm volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static uint32_t pci_rd(int bus, int slot, int off)
{
    outl(0xCF8, 0x80000000u | (uint32_t)(bus << 16) | (uint32_t)(slot << 11) | (uint32_t)(off & 0xFC));
    return inl(0xCFC);
}
static void pci_wr(int bus, int slot, int off, uint32_t v)
{
    outl(0xCF8, 0x80000000u | (uint32_t)(bus << 16) | (uint32_t)(slot << 11) | (uint32_t)(off & 0xFC));
    outl(0xCFC, v);
}
const uint8_t *e1000_mac(void) { return mac; }
int e1000_init(void)
{
    int bus = -1, slot = -1;
    for (int b = 0; b < 8 && bus < 0; b++)
        for (int s = 0; s < 32; s++)
            if (pci_rd(b, s, 0) == 0x100E8086u) { bus = b; slot = s; break; }
    if (bus < 0)
        return 0;
    pci_wr(bus, slot, 4, pci_rd(bus, slot, 4) | 0x6);
    uint32_t bar = pci_rd(bus, slot, 0x10);
    if (bar & 1 || (bar & 6) == 4)
        return 0;
    mmio = (volatile uint8_t *)(uint64_t)(bar & ~0xFu);
    uint32_t ral = rd(R_RAL0), rah = rd(R_RAH0);
    for (int i = 0; i < 4; i++) mac[i] = (uint8_t)(ral >> (8 * i));
    mac[4] = (uint8_t)rah; mac[5] = (uint8_t)(rah >> 8);
    wr(R_IMC, 0xFFFFFFFFu);
    rd(R_ICR);
    wr(R_CTRL, rd(R_CTRL) | (1u << 6));
    for (int i = 0; i < 128; i++) wr(R_MTA + 4 * i, 0);
    rx = pmm_alloc_contig(1);
    tx = pmm_alloc_contig(1);
    rxbuf = pmm_alloc_contig(NRX * BUFSZ / PAGE_SIZE);
    txbuf = pmm_alloc_contig(NTX * BUFSZ / PAGE_SIZE);
    if (!rx || !tx || !rxbuf || !txbuf)
        return 0;
    for (int i = 0; i < NRX; i++)
    {
        rx[i].addr = (uint64_t)(rxbuf + i * BUFSZ);
        rx[i].status = 0;
    }
    for (int i = 0; i < NTX; i++)
    {
        tx[i].addr = (uint64_t)(txbuf + i * BUFSZ);
        tx[i].status = 1;
    }
    wr(R_RDBAL, (uint32_t)(uint64_t)rx); wr(R_RDBAH, 0);
    wr(R_RDLEN, NRX * sizeof(struct rxd));
    wr(R_RDH, 0); wr(R_RDT, NRX - 1);
    wr(R_RCTL, (1u << 1) | (1u << 15) | (1u << 26));
    wr(R_TDBAL, (uint32_t)(uint64_t)tx); wr(R_TDBAH, 0);
    wr(R_TDLEN, NTX * sizeof(struct txd));
    wr(R_TDH, 0); wr(R_TDT, 0);
    wr(R_TIPG, 0x0060200A);
    wr(R_TCTL, (1u << 1) | (1u << 3) | (0x10u << 4) | (0x40u << 12));
    rx_next = 0; tx_next = 0;
    uint32_t il = pci_rd(bus, slot, 0x3C) & 0xFF;
    if (il > 0 && il < 16 && il != 1 && il != 4)
    {
        irq_line = (int)il;
        pic_unmask(irq_line);
        rd(R_ICR);
        wr(R_IMS, (1u << 7) | (1u << 6) | (1u << 4) | (1u << 2));
    }
    return 1;
}
int e1000_send(const void *frame, uint32_t len)
{
    if (!mmio || len == 0 || len > 1514)
        return -1;
    struct txd *d = &tx[tx_next];
    if (!(d->status & 1))
        return -1;
    uint8_t *dst = txbuf + tx_next * BUFSZ;
    asm volatile("rep movsb" : "+D"(dst), "+S"(frame), "+c"(len) : : "memory");
    d->len = (uint16_t)(dst - (txbuf + tx_next * BUFSZ));
    d->cmd = 0x0B;
    d->status = 0;
    tx_next = (tx_next + 1) % NTX;
    wr(R_TDT, tx_next);
    return 0;
}
void e1000_poll(void (*cb)(const uint8_t *, uint32_t))
{
    if (!mmio)
        return;
    while (rx[rx_next].status & 1)
    {
        struct rxd *d = &rx[rx_next];
        if (!d->err)
            cb(rxbuf + rx_next * BUFSZ, d->len);
        d->status = 0;
        wr(R_RDT, rx_next);
        rx_next = (rx_next + 1) % NRX;
    }
}
int e1000_irq_line(void) { return irq_line; }
void e1000_irq(void)
{
    if (!mmio)
        return;
    rd(R_ICR);
    net_poll();
}
