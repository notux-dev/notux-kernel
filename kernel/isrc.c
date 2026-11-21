#include "types.h"
#include "vga.h"
#include "stdint.h"
#include "serial.h"
#include "lapic/lapic.h"
void timer_interrupt_handler(void) {
    serwrite(".");
    lapic_eoi();
}
#include "process.h"
#include "vmm.h"
static void hexw(uint64_t v)
{
    char b[17];
    for (int i = 0; i < 16; i++)
        b[i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
    b[16] = 0;
    serwrite(b);
}
void isr_handler(uint64_t *rsp) {
    uint64_t vec = rsp[15];
    if (vec == 14 && (rsp[18] & 3) == 3 && (rsp[16] & 3) == 3)
    {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        process_t *cur = get_current_process();
        if (cur && cr2 <= 0x7FFFFFFFFFFFULL && vmm_cow_fault(cur->pml4, cr2 & ~0xFFFULL) > 0)
            return;
    }
    if (vec == 14 && (rsp[18] & 3) == 3 && !(rsp[16] & 1))
    {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        process_t *cur = get_current_process();
        if (cur && cr2 <= 0x7FFFFFFFFFFFULL &&
            vmm_lazy_fault(cur->pml4, cr2 & ~0xFFFULL, (rsp[16] & 2) != 0) > 0)
            return;
    }
    if (vec < 32 && (rsp[18] & 3) == 3)
    {
        int sig = 11;
        if (vec == 6) sig = 4;
        else if (vec == 0 || vec == 16 || vec == 19) sig = 8;
        else if (vec == 17) sig = 7;
        else if (vec == 1 || vec == 3) sig = 5;
        serwrite("USER FAULT vec=");
        char b[4] = { '0' + (char)(vec / 10), '0' + (char)(vec % 10), '\n', 0 };
        serwrite(b);
        serwrite("  rip="); hexw(rsp[17]);
        serwrite(" err="); hexw(rsp[16]);
        serwrite(" rsp="); hexw(rsp[20]);
        { uint64_t cr2; asm volatile("mov %%cr2, %0" : "=r"(cr2)); serwrite(" cr2="); hexw(cr2); }
        serwrite("\n");
        exit_by_signal(sig);
    }
    print("ISR triggered", 12, 0x4F);
}
