#include "lapic.h"
#include "../types.h"
#define LAPIC_ID         0x020  
#define LAPIC_VER        0x030  
#define LAPIC_TPR        0x080  
#define LAPIC_EOI        0x0B0  
#define LAPIC_SVR        0x0F0  
#define LAPIC_LVT_TIMER  0x320  
#define LAPIC_TIMER_ICR  0x380  
#define LAPIC_TIMER_CCR  0x390  
#define LAPIC_TIMER_DCR  0x3E0  
#define IA32_APIC_BASE_MSR 0x1B
static uintptr_t lapic_base = 0xFEE00000;
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}
static inline void wrmsr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" : : "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void lapic_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t *)(lapic_base + reg) = value;
}
static inline uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t *)(lapic_base + reg);
}
void pic_disable(void) {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}
void lapic_init(uintptr_t virt_base) {
    if (virt_base != 0) {
        lapic_base = virt_base;
    }
    pic_disable();
    uint64_t msr = rdmsr(IA32_APIC_BASE_MSR);
    wrmsr(IA32_APIC_BASE_MSR, msr | (1 << 11));
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_SVR, 0x1FF);
}
void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}
uint32_t lapic_get_id(void) {
    return lapic_read(LAPIC_ID) >> 24;
}
void lapic_timer_init(uint32_t ticks) {
    lapic_write(LAPIC_TIMER_DCR, 0x3);
    lapic_write(LAPIC_LVT_TIMER, 32 | (1 << 17));
    lapic_write(LAPIC_TIMER_ICR, ticks);
}
