#include "panic.h"
#include "panicl.h"
#include "console.h"
#include "vbe.h"
#include "stdint.h"
extern void serwrite(char *str);
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600
#define PANIC_BG_COLOR   0x00330000u 
#define PANIC_TEXT_COLOR 0x00FF5555u 
#define PANIC_HDR_COLOR  0x00FFFFFFu 
#define LOGO_MARGIN_X 16
#define LOGO_MARGIN_Y 16
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr0, cr2, cr3, cr4;
} panic_regs_t;
static inline void capture_registers(panic_regs_t *r) {
    asm volatile ("mov %%rax, %0" : "=m"(r->rax));
    asm volatile ("mov %%rbx, %0" : "=m"(r->rbx));
    asm volatile ("mov %%rcx, %0" : "=m"(r->rcx));
    asm volatile ("mov %%rdx, %0" : "=m"(r->rdx));
    asm volatile ("mov %%rsi, %0" : "=m"(r->rsi));
    asm volatile ("mov %%rdi, %0" : "=m"(r->rdi));
    asm volatile ("mov %%rbp, %0" : "=m"(r->rbp));
    asm volatile ("mov %%rsp, %0" : "=m"(r->rsp));
    asm volatile ("mov %%r8,  %0" : "=m"(r->r8));
    asm volatile ("mov %%r9,  %0" : "=m"(r->r9));
    asm volatile ("mov %%r10, %0" : "=m"(r->r10));
    asm volatile ("mov %%r11, %0" : "=m"(r->r11));
    asm volatile ("mov %%r12, %0" : "=m"(r->r12));
    asm volatile ("mov %%r13, %0" : "=m"(r->r13));
    asm volatile ("mov %%r14, %0" : "=m"(r->r14));
    asm volatile ("mov %%r15, %0" : "=m"(r->r15));
    asm volatile ("mov %%cr0, %%rax; mov %%rax, %0" : "=m"(r->cr0) :: "rax");
    asm volatile ("mov %%cr2, %%rax; mov %%rax, %0" : "=m"(r->cr2) :: "rax");
    asm volatile ("mov %%cr3, %%rax; mov %%rax, %0" : "=m"(r->cr3) :: "rax");
    asm volatile ("mov %%cr4, %%rax; mov %%rax, %0" : "=m"(r->cr4) :: "rax");
    asm volatile ("pushfq; pop %%rax; mov %%rax, %0" : "=m"(r->rflags) :: "rax");
    void *here = &&label_here;
    r->rip = (uint64_t)here;
label_here:
    return;
}
static void hex_to_str(uint64_t value, char *out ) {
    const char *digits = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int shift = (15 - i) * 4;
        out[2 + i] = digits[(value >> shift) & 0xF];
    }
    out[18] = 0;
}
static void print_reg_line(const char *name, uint64_t value) {
    char hexbuf[19];
    hex_to_str(value, hexbuf);
    kprint((char *)name, PANIC_TEXT_COLOR);
    kprint(hexbuf, PANIC_TEXT_COLOR);
    kprint("\n", PANIC_TEXT_COLOR);
    serwrite((char *)name);
    serwrite(hexbuf);
    serwrite("\n");
}
static void fill_screen(uint32_t color) {
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)vbeaddr();
    if (!fb) return;
    uint32_t total = SCREEN_WIDTH * SCREEN_HEIGHT;
    for (uint32_t i = 0; i < total; i++) {
        fb[i] = color;
    }
}
static void draw_logo(uint32_t x0, uint32_t y0) {
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)vbeaddr();
    if (!fb) return;
    for (uint32_t y = 0; y < image_height; y++) {
        if (y0 + y >= SCREEN_HEIGHT) break;
        for (uint32_t x = 0; x < image_width; x++) {
            if (x0 + x >= SCREEN_WIDTH) break;
            uint32_t pixel = image_data[y * image_width + x];
            fb[(y0 + y) * SCREEN_WIDTH + (x0 + x)] = pixel;
        }
    }
}
void panic(const char *message) {
    asm volatile ("cli");
    panic_regs_t regs;
    capture_registers(&regs);
    fill_screen(PANIC_BG_COLOR);
    draw_logo(LOGO_MARGIN_X, LOGO_MARGIN_Y);
    kprint("\n\n            KERNEL PANIC\n\n", PANIC_HDR_COLOR);
    serwrite("\n\n==== KERNEL PANIC ====\n");
    kprint("Reason: ", PANIC_HDR_COLOR);
    kprint((char *)message, PANIC_TEXT_COLOR);
    kprint("\n\n", PANIC_TEXT_COLOR);
    serwrite("Reason: ");
    serwrite((char *)message);
    serwrite("\n\n");
    kprint("-- Register dump --\n", PANIC_HDR_COLOR);
    serwrite("-- Register dump --\n");
    print_reg_line("RAX=", regs.rax);
    print_reg_line("RBX=", regs.rbx);
    print_reg_line("RCX=", regs.rcx);
    print_reg_line("RDX=", regs.rdx);
    print_reg_line("RSI=", regs.rsi);
    print_reg_line("RDI=", regs.rdi);
    print_reg_line("RBP=", regs.rbp);
    print_reg_line("RSP=", regs.rsp);
    print_reg_line("R8 =", regs.r8);
    print_reg_line("R9 =", regs.r9);
    print_reg_line("R10=", regs.r10);
    print_reg_line("R11=", regs.r11);
    print_reg_line("R12=", regs.r12);
    print_reg_line("R13=", regs.r13);
    print_reg_line("R14=", regs.r14);
    print_reg_line("R15=", regs.r15);
    print_reg_line("RIP~", regs.rip);
    print_reg_line("FLAG=", regs.rflags);
    print_reg_line("CR0=", regs.cr0);
    print_reg_line("CR2=", regs.cr2);
    print_reg_line("CR3=", regs.cr3);
    print_reg_line("CR4=", regs.cr4);
    kprint("\nSystem halted.\n", PANIC_HDR_COLOR);
    serwrite("\nSystem halted.\n");
    for (;;) {
        asm volatile ("hlt");
    }
}
