#include "pci.h"
#include "io.h"
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static uint32_t pciread(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1U << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}
uint32_t pcibar0(void) {
    for (int slot = 0; slot < 32; slot++) {
        uint32_t id = pciread(0, slot, 0, 0);
        if ((id & 0xFFFF) == 0xFFFF) continue;
        uint32_t classcode = pciread(0, slot, 0, 0x08);
        uint8_t base_class = (classcode >> 24) & 0xFF;
        if (base_class == 0x03) {
            uint32_t bar0 = pciread(0, slot, 0, 0x10);
            return bar0 & 0xFFFFFFF0;
        }
    }
    return 0;
}
