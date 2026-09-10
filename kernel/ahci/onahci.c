#include "ahci.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#define AHCI_MAX_PORTS 32
#define AHCI_CLASS     0x01
#define AHCI_SUBCLASS  0x06
static hba_mem_t     *g_abar = NULL;
static ahci_device_t  g_devices[AHCI_MAX_PORTS];
static int             g_device_count = 0;
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) |
                       (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(0xCF8, address);
    return inl(0xCFC);
}
static void *k_memset(void *dst, int val, uint64_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = (uint8_t)val;
    return dst;
}
static void *alloc_mapped_page(uint64_t flags) {
    void *phys = pmm_alloc_page();
    if (!phys) return NULL;
    uint64_t *pml4 = vmm_get_kernel_pml4();
    vmm_map_page(pml4, (uint64_t)phys, (uint64_t)phys, flags);
    k_memset(phys, 0, PAGE_SIZE);
    return phys;
}
static hba_mem_t *ahci_find_controller(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read32(bus, slot, 0, 0x00);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            uint32_t class_rev = pci_read32(bus, slot, 0, 0x08);
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            uint8_t subclass   = (class_rev >> 16) & 0xFF;
            if (class_code == AHCI_CLASS && subclass == AHCI_SUBCLASS) {
                uint32_t bar5 = pci_read32(bus, slot, 0, 0x24);
                bar5 &= ~0xFu; 
                if (!bar5) return NULL;
                uint64_t *pml4 = vmm_get_kernel_pml4();
                vmm_map_page(pml4, (uint64_t)bar5, (uint64_t)bar5, PAGE_PRESENT | PAGE_WRITE);
                return (hba_mem_t *)(uintptr_t)bar5;
            }
        }
    }
    return NULL;
}
static void ahci_global_init(hba_mem_t *abar) {
    abar->ghc |= GHC_AE;
}
static int ahci_port_is_present(hba_port_t *port) {
    uint32_t ssts = port->ssts;
    uint8_t  det  = ssts & 0xF;
    uint8_t  ipm  = (ssts >> 8) & 0xF;
    return (det == SSTS_DET_PRESENT) && (ipm == 1);
}
static void ahci_port_stop(hba_port_t *port) {
    port->cmd &= ~PXCMD_ST;
    port->cmd &= ~PXCMD_FRE;
    while (port->cmd & (PXCMD_FR | PXCMD_CR)) { }
}
static void ahci_port_start(hba_port_t *port) {
    while (port->cmd & PXCMD_CR) { }
    port->cmd |= PXCMD_FRE;
    port->cmd |= PXCMD_ST;
}
static void ahci_port_init(ahci_device_t *dev) {
    hba_port_t *port = dev->port;
    ahci_port_stop(port);
    void *clb = alloc_mapped_page(PAGE_PRESENT | PAGE_WRITE);
    dev->clb_virt = clb;
    port->clb  = (uint32_t)(uintptr_t)clb;
    port->clbu = (uint32_t)((uintptr_t)clb >> 32);
    void *fb = alloc_mapped_page(PAGE_PRESENT | PAGE_WRITE);
    dev->fb_virt = fb;
    port->fb  = (uint32_t)(uintptr_t)fb;
    port->fbu = (uint32_t)((uintptr_t)fb >> 32);
    hba_cmd_header_t *hdrs = (hba_cmd_header_t *)dev->clb_virt;
    for (int i = 0; i < 32; i++) {
        void *ctba = alloc_mapped_page(PAGE_PRESENT | PAGE_WRITE);
        dev->ctba_virt[i] = ctba;
        hdrs[i].ctba  = (uint32_t)(uintptr_t)ctba;
        hdrs[i].ctbau = (uint32_t)((uintptr_t)ctba >> 32);
        hdrs[i].prdtl = AHCI_PRDT_ENTRIES;
    }
    port->serr = port->serr;
    ahci_port_start(port);
}
static int ahci_find_free_slot(hba_port_t *port) {
    uint32_t busy = port->sact | port->ci;
    for (int i = 0; i < 32; i++)
        if (!(busy & (1u << i))) return i;
    return -1;
}
static int ahci_do_rw(ahci_device_t *dev, uint64_t lba, uint32_t count, void *buf, int write) {
    hba_port_t *port = dev->port;
    int slot = ahci_find_free_slot(port);
    if (slot < 0) return -1;
    hba_cmd_header_t *hdr = &((hba_cmd_header_t *)dev->clb_virt)[slot];
    hdr->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    hdr->w   = write ? 1 : 0;
    hdr->prdtl = 1;
    hba_cmd_tbl_t *tbl = (hba_cmd_tbl_t *)dev->ctba_virt[slot];
    k_memset(tbl, 0, sizeof(hba_cmd_tbl_t));
    uintptr_t buf_phys = (uintptr_t)buf;
    tbl->prdt[0].dba  = (uint32_t)buf_phys;
    tbl->prdt[0].dbau = (uint32_t)(buf_phys >> 32);
    tbl->prdt[0].dbc  = (count * 512) - 1;
    tbl->prdt[0].i    = 1;
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c        = 1;
    fis->command  = write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
    fis->lba0 = lba & 0xFF;        fis->lba1 = (lba >> 8) & 0xFF;  fis->lba2 = (lba >> 16) & 0xFF;
    fis->lba3 = (lba >> 24) & 0xFF; fis->lba4 = (lba >> 32) & 0xFF; fis->lba5 = (lba >> 40) & 0xFF;
    fis->device = 1 << 6;
    fis->count = count;
    while (port->tfd & 0x88) { }
    port->ci |= (1u << slot);
    while (port->ci & (1u << slot)) {
        if (port->is & (1u << 30)) return -1;
    }
    return 0;
}
int ahci_read_sectors(ahci_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    return ahci_do_rw(dev, lba, count, buf, 0);
}
int ahci_write_sectors(ahci_device_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    return ahci_do_rw(dev, lba, count, (void *)buf, 1);
}
ahci_device_t *ahci_get_device(int index) {
    if (index < 0 || index >= g_device_count) return NULL;
    return &g_devices[index];
}
int ahci_init(void) {
    g_abar = ahci_find_controller();
    if (!g_abar) return -1;
    ahci_global_init(g_abar);
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(g_abar->pi & (1u << i))) continue;
        hba_port_t *port = (hba_port_t *)((uint8_t *)g_abar + 0x100 + i * 0x80);
        if (!ahci_port_is_present(port)) continue;
        ahci_device_t *dev = &g_devices[g_device_count];
        dev->port = port;
        dev->port_num = i;
        ahci_port_init(dev);
        g_device_count++;
    }
    return g_device_count;
}
