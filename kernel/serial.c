#include "serial.h"
#include "io.h"
#define COM1 0x3F8
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
void serinit(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0x07);
    outb(COM1 + 4, 0x0B);
    outb(COM1 + 1, 0x01);
}
static int serready(void) {
    return inb(COM1 + 5) & 0x20;
}
void serwrite(const char *msg) {
    while (*msg) {
        while (!serready());
        outb(COM1, *msg);
        msg++;
    }
}

void serputc(char c) {
    while (!serready());
    outb(COM1, (uint8_t)c);
}
int serrx_ready(void) { return inb(COM1 + 5) & 1; }
uint8_t serrx_get(void) { return inb(COM1); }
