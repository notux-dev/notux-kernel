#ifndef IDT_H
#define IDT_H
#include "types.h"
struct ientry {
    uint16_t base_low; 
    uint16_t sel;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved; 
} __attribute__((packed));
struct iptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
void iinit(void);
void iset(int num, uint64_t base, uint16_t sel, uint8_t flags);
#endif
