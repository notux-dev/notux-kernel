#ifndef INPUT_H
#define INPUT_H 
#include "stdint.h"
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_MSC 0x04
#define EV_MAX 0x1f
#define SYN_REPORT 0
#define SYN_DROPPED 3
#define KEY_MAX 0x2ff
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114
#define REL_X 0x00
#define REL_Y 0x01
#define REL_HWHEEL 0x06
#define REL_WHEEL 0x08
#define REL_MAX 0x0f
#define ABS_MAX 0x3f
#define BUS_I8042 0x11
struct input_event {
    int64_t sec, usec;
    uint16_t type, code;
    int32_t value;
};
struct input_absinfo { int32_t value, minimum, maximum, fuzz, flat, resolution; };
#define INPUT_MAXDEV 4
struct input_dev {
    char name[64];
    char phys[32];
    uint16_t bustype, vendor, product, version;
    uint8_t evbit[(EV_MAX + 8) / 8];
    uint8_t keybit[(KEY_MAX + 8) / 8];
    uint8_t relbit[(REL_MAX + 8) / 8];
    uint8_t absbit[(ABS_MAX + 8) / 8];
    uint8_t key[(KEY_MAX + 8) / 8];
    struct input_absinfo abs[ABS_MAX + 1];
    int index;
    int grab;
    int nev;
};
static inline void input_set_bit(uint8_t *a, int b) { a[b >> 3] |= (uint8_t)(1u << (b & 7)); }
static inline int input_test_bit(const uint8_t *a, int b) { return (a[b >> 3] >> (b & 7)) & 1; }
int input_register(struct input_dev *d);
void input_event(struct input_dev *d, uint16_t type, uint16_t code, int32_t value);
void input_report_key(struct input_dev *d, uint16_t code, int value);
void input_report_rel(struct input_dev *d, uint16_t code, int value);
void input_report_abs(struct input_dev *d, uint16_t code, int value);
void input_sync(struct input_dev *d);
void kbdev_init(void);
void kbdev_scancode(uint8_t sc);
void ps2mouse_init(void);
void ps2mouse_byte(uint8_t b);
#endif
