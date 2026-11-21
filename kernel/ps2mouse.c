#include "input.h"
#include "pic.h"
#include "serial.h"
#include "ktime.h"
#include "stdint.h"
static inline uint8_t inb_(uint16_t p) { uint8_t r; asm volatile ("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outb_(uint16_t p, uint8_t v) { asm volatile ("outb %0, %1" : : "a"(v), "Nd"(p)); }
#define I8042_DATA 0x60
#define I8042_CMD 0x64
static struct input_dev mouse;
static int have_mouse, wheel;
static uint8_t pkt[4];
static int pn;
static uint64_t last_byte_tick;
static void wait_in(void) { for (int i = 0; i < 100000 && (inb_(I8042_CMD) & 2); i++) ; }
static int aux_read(void)
{
    for (int i = 0; i < 200000; i++)
    {
        uint8_t st = inb_(I8042_CMD);
        if (st & 1)
        {
            uint8_t b = inb_(I8042_DATA);
            if (st & 0x20)
                return b;
        }
    }
    return -1;
}
static int ctl_read(void)
{
    for (int i = 0; i < 200000; i++)
        if (inb_(I8042_CMD) & 1)
            return inb_(I8042_DATA);
    return -1;
}
static int aux_write(uint8_t b)
{
    for (int tries = 0; tries < 3; tries++)
    {
        wait_in(); outb_(I8042_CMD, 0xD4);
        wait_in(); outb_(I8042_DATA, b);
        int r = aux_read();
        if (r == 0xFA)
            return 0;
        if (r != 0xFE)
            return -1;
    }
    return -1;
}
static int aux_cmd2(uint8_t c, uint8_t arg) { return aux_write(c) || aux_write(arg); }
void ps2mouse_init(void)
{
    for (int i = 0; i < 16 && (inb_(I8042_CMD) & 1); i++)
        inb_(I8042_DATA);
    wait_in(); outb_(I8042_CMD, 0xA8);
    if (aux_write(0xFF) || aux_read() != 0xAA || aux_read() != 0x00)
    {
        wait_in(); outb_(I8042_CMD, 0xA7);
        return;
    }
    aux_write(0xF5);
    aux_cmd2(0xF3, 200); aux_cmd2(0xF3, 100); aux_cmd2(0xF3, 80);
    int id = -1;
    if (aux_write(0xF2) == 0)
        id = aux_read();
    wheel = (id == 3);
    aux_write(0xF6);
    aux_cmd2(0xF3, 100);
    aux_cmd2(0xE8, 3);
    if (aux_write(0xF4))
    {
        wait_in(); outb_(I8042_CMD, 0xA7);
        return;
    }
    wait_in(); outb_(I8042_CMD, 0x20);
    int cfg = ctl_read();
    if (cfg < 0)
        cfg = 0x47;
    cfg = (cfg | 0x02) & ~0x20;
    wait_in(); outb_(I8042_CMD, 0x60);
    wait_in(); outb_(I8042_DATA, (uint8_t)cfg);
    mouse.name[0] = 0;
    const char *nm = wheel ? "ImPS/2 Generic Wheel Mouse" : "PS/2 Generic Mouse";
    for (int i = 0; nm[i] && i < 63; i++) { mouse.name[i] = nm[i]; mouse.name[i + 1] = 0; }
    const char *ph = "isa0060/serio1/input0";
    for (int i = 0; ph[i]; i++) { mouse.phys[i] = ph[i]; mouse.phys[i + 1] = 0; }
    mouse.bustype = BUS_I8042; mouse.vendor = 0x0002; mouse.product = wheel ? 5 : 1; mouse.version = 0;
    input_set_bit(mouse.evbit, EV_SYN); input_set_bit(mouse.evbit, EV_KEY); input_set_bit(mouse.evbit, EV_REL);
    input_set_bit(mouse.keybit, BTN_LEFT); input_set_bit(mouse.keybit, BTN_RIGHT); input_set_bit(mouse.keybit, BTN_MIDDLE);
    input_set_bit(mouse.relbit, REL_X); input_set_bit(mouse.relbit, REL_Y);
    if (wheel)
        input_set_bit(mouse.relbit, REL_WHEEL);
    if (input_register(&mouse) < 0)
        return;
    have_mouse = 1;
    pic_unmask(12);
    serwrite("INPUT: ");
    serwrite(mouse.name);
    serwrite(" as /dev/input/event0\n");
}
void ps2mouse_byte(uint8_t b)
{
    if (!have_mouse)
        return;
    uint64_t now = ktime_ticks;
    if (pn && now - last_byte_tick > 50)
        pn = 0;
    last_byte_tick = now;
    if (pn == 0 && !(b & 0x08))
        return;
    pkt[pn++] = b;
    if (pn < (wheel ? 4 : 3))
        return;
    pn = 0;
    int dx = pkt[1] - ((pkt[0] << 4) & 0x100);
    int dy = pkt[2] - ((pkt[0] << 3) & 0x100);
    int dz = 0;
    if (wheel)
    {
        dz = pkt[3] & 0x0F;
        if (dz & 8)
            dz -= 16;
    }
    input_report_key(&mouse, BTN_LEFT, pkt[0] & 1);
    input_report_key(&mouse, BTN_RIGHT, pkt[0] & 2);
    input_report_key(&mouse, BTN_MIDDLE, pkt[0] & 4);
    input_report_rel(&mouse, REL_X, dx);
    input_report_rel(&mouse, REL_Y, -dy);
    input_report_rel(&mouse, REL_WHEEL, -dz);
    input_sync(&mouse);
}
