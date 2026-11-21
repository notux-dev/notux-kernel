#include "input.h"
#include "serial.h"
#include "stdint.h"
static struct input_dev kbd;
static int have_kbd = 0;
static int e0 = 0, e1_skip = 0;
static const struct { uint8_t sc; uint16_t key; } ext[] = {
    {0x1C, 96}, {0x1D, 97}, {0x35, 98}, {0x37, 99},
    {0x38, 100}, {0x47, 102}, {0x48, 103}, {0x49, 104},
    {0x4B, 105}, {0x4D, 106}, {0x4F, 107}, {0x50, 108},
    {0x51, 109}, {0x52, 110}, {0x53, 111}, {0x5B, 125},
    {0x5C, 126}, {0x5D, 127},
};
void kbdev_init(void)
{
    const char *nm = "AT Translated Set 2 keyboard";
    for (int i = 0; nm[i]; i++) { kbd.name[i] = nm[i]; kbd.name[i + 1] = 0; }
    const char *ph = "isa0060/serio0/input0";
    for (int i = 0; ph[i]; i++) { kbd.phys[i] = ph[i]; kbd.phys[i + 1] = 0; }
    kbd.bustype = BUS_I8042; kbd.vendor = 0x0001; kbd.product = 0x0001; kbd.version = 0xAB41;
    input_set_bit(kbd.evbit, EV_SYN);
    input_set_bit(kbd.evbit, EV_KEY);
    for (int k = 1; k <= 0x58; k++)
        input_set_bit(kbd.keybit, k);
    for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++)
        input_set_bit(kbd.keybit, ext[i].key);
    if (input_register(&kbd) < 0)
        return;
    have_kbd = 1;
    serwrite("INPUT: AT Translated Set 2 keyboard as /dev/input/event");
    serwrite(kbd.index == 0 ? "0\n" : kbd.index == 1 ? "1\n" : kbd.index == 2 ? "2\n" : "3\n");
}
void kbdev_scancode(uint8_t sc)
{
    if (!have_kbd)
        return;
    if (e1_skip) { e1_skip--; return; }
    if (sc == 0xE1) { e1_skip = 5; return; }
    if (sc == 0xE0) { e0 = 1; return; }
    int rel = sc & 0x80;
    uint8_t code = sc & 0x7F;
    uint16_t key = 0;
    if (e0)
    {
        e0 = 0;
        if (code == 0x2A || code == 0x36)
            return;
        for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++)
            if (ext[i].sc == code) { key = ext[i].key; break; }
    }
    else if (code >= 1 && code <= 0x58)
        key = code;
    if (!key)
        return;
    input_report_key(&kbd, key, rel ? 0 : 1);
    input_sync(&kbd);
}
