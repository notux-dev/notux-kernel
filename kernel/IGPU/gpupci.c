#include "igpu.h"
#include "console.h"
uint64_t gpu_mmio_base = 0;
static void kprint_hex64(uint64_t val, uint32_t color) {
    char buf[19] = "0x0000000000000000";
    const char hex[] = "0123456789ABCDEF";
    for (int i = 17; i >= 2; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    kprint(buf, color);
}
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = (1U << 31)
                     | ((uint32_t)bus << 16)
                     | ((uint32_t)dev << 11)
                     | ((uint32_t)func << 8)
                     | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}
int igpu_pci_init(void) {
    uint32_t color_white = 0xFFFFFF;
    uint32_t color_green = 0x00FF00;
    uint32_t color_red   = 0xFF0000;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id_reg = pci_read_config((uint8_t)bus, dev, func, 0x00);
                uint16_t vendor_id = id_reg & 0xFFFF;
                if (vendor_id != 0x8086) {
                    continue;
                }
                uint32_t class_reg = pci_read_config((uint8_t)bus, dev, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                if (class_code == 0x03) { 
                    uint32_t bar0 = pci_read_config((uint8_t)bus, dev, func, 0x10);
                    uint64_t phys_addr = bar0 & ~0xFUL;
                    if ((bar0 & 0x6) == 0x4) {
                        uint32_t bar1 = pci_read_config((uint8_t)bus, dev, func, 0x14);
                        phys_addr |= ((uint64_t)bar1 << 32);
                    }
                    gpu_mmio_base = phys_addr;
                    kprint("[GPU] Intel iGPU BAR0 MMIO: ", color_white);
                    kprint_hex64(gpu_mmio_base, color_green);
                    kprint("\n", color_white);
                    return 1;
                }
            }
        }
    }
    gpu_mmio_base = 0;
    kprint("[GPU] Intel iGPU not found\n", color_red);
    return 0;
}
