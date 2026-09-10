#include "types.h"
#include "vga.h"
#include "stdint.h"
#include "serial.h"
#include "lapic/lapic.h"
void timer_interrupt_handler(void) {
    serwrite(".");
    lapic_eoi();
}
void isr_handler(uint64_t *rsp) {
    print("ISR triggered", 12, 0x4F);
}
