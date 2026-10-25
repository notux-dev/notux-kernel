#include "gdt.h"
struct entry gdt[7];
struct ptr gp;
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;
static tss_entry_t tss_entry;
extern void flush(uint64_t);
static void gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}
static void tss_gate(int num, uint64_t base, uint32_t limit) {
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].access      = 0x89;
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    struct entry *upper = &gdt[num + 1];
    *(uint32_t *)upper = (base >> 32);
    *((uint32_t *)upper + 1) = 0;
}
/* THE REAL, ACTIVE TSS is boot/boot.asm's `tss64` -- that is the one
 * `ltr` actually loads there at boot (see boot.asm: it builds its own
 * GDT including a TSS descriptor pointing at tss64, then `ltr ax`
 * loads that selector). This file's `tss_entry`/`init()` is dead code
 * (boot.asm's gdt64 table is what's really active, same story as
 * kernel/gdt.c's init() never being called for the GDT itself) -- so
 * writing into tss_entry.rsp0 here was a complete no-op: the CPU's
 * real RSP0 (used for every ring3->ring0 privilege-elevating
 * interrupt/syscall, for EVERY process) stayed hardcoded forever at
 * whatever boot.asm set it to once at boot (`stack_top`, the kernel's
 * own one-off 16KB startup stack -- see `mov [tss64+4], rax` right
 * before the `ltr` in boot.asm).
 *
 * Consequence while this was broken: activate_process() believed it
 * was giving each process its own kernel stack, but in reality EVERY
 * process's syscalls and interrupt entries landed on that single
 * shared boot stack, regardless of which process was "current". Two
 * processes trading off (e.g. a parent suspended mid wait4() while a
 * child it spawned runs and exits) would silently clobber each
 * other's in-flight register frame on that shared stack -- the parent
 * resumes into whatever garbage the child's own syscalls left behind
 * in the same bytes, typically a bogus RIP that immediately page
 * faults. This was invisible with a single always-running process
 * (the common case so far), which is exactly why it went unnoticed.
 *
 * Fix: write RSP0 into the struct that's actually loaded. x86_64 TSS
 * layout: byte 0 = reserved (4 bytes), bytes 4..11 = RSP0. tss64 is a
 * raw byte buffer (`resb 104`) in boot.asm, so poke that offset
 * directly -- mirrors exactly what boot.asm's own
 * `mov [tss64 + 4], rax` does for the initial value. */
extern uint8_t tss64[];
void set_kernel_stack(uint64_t stack) {
    tss_entry.rsp0 = stack; /* kept in sync too, in case anything
                                 * else ever reads this struct back */
    *(uint64_t *)(tss64 + 4) = stack;
}
void init(void) {
    gp.limit = (sizeof(struct entry) * 7) - 1;
    gp.base  = (uint64_t)&gdt;
    gate(0, 0, 0, 0, 0);
    gate(1, 0, 0, 0x9A, 0x20);
    gate(2, 0, 0, 0x92, 0x00);
    gate(3, 0, 0, 0xFA, 0x20);
    gate(4, 0, 0, 0xF2, 0x00);
    unsigned char *p = (unsigned char *)&tss_entry;
    for (unsigned int i = 0; i < sizeof(tss_entry); i++) {
        p[i] = 0;
    }
    tss_entry.rsp0 = 0x90000;
    tss_entry.iomap_base = sizeof(tss_entry);
    tss_gate(5, (uint64_t)&tss_entry, sizeof(tss_entry) - 1);
    flush((uint64_t)&gp);
    asm volatile("ltr %%ax" :: "a" (0x28));
}
