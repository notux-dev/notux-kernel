#include "pic.h"
#include "io.h"
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) {
    outb(0x80, 0);
}
#define PIC1        0x20
#define PIC2        0xA0
#define PIC1_DATA   0x21
#define PIC2_DATA   0xA1
#define PIC_EOI     0x20
void picremap(void) {
		outb(PIC1, 0x11); io_wait();
		outb(PIC2, 0x11); io_wait();
		outb(PIC1_DATA, 0x20); io_wait();
		outb(PIC2_DATA, 0x28); io_wait();
		outb(PIC1_DATA, 4);		io_wait();
		outb(PIC2_DATA, 2);		io_wait();
		outb(PIC1_DATA, 0x01);  io_wait();
		outb(PIC2_DATA, 0x01);  io_wait();
}
void piceoi(int irq) {
    if (irq >= 8) {
        outb(PIC2, PIC_EOI);
    }
    outb(PIC1, PIC_EOI);
}
