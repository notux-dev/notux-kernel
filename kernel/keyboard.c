#include "pic.h"
#include "serial.h"
#include "vga.h"
#include "vbe.h"
#include "console.h"
#include "stdint.h"
#include "lapic/lapic.h"
uint64_t current_irq_no = 0;
#define KB_BUF_SIZE 64
static char kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_head = 0;
static volatile uint32_t kb_tail = 0;
static int e0_prefix = 0;
static int shift_pressed = 0;
static const char scancode_ascii[] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',  '7',
    '8',  '9',   '0',  '-',  '=',  '\b', '\t', 'q',
    'w',  'e',   'r',  't',  'y',  'u',  'i',  'o',
    'p',  '[',   ']',  '\n',  0,    'a',  's',  'd',
    'f',  'g',   'h',  'j',  'k',  'l',  ';',  '\'',
    '`',  0,     '\\', 'z',  'x',  'c',  'v',  'b',  'n',
    'm',  ',',   '.',  '/',  0,    '*',  0,    ' '
};
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile (
        "inb %1, %0"
        : "=a"(ret)
        : "Nd"(port)
    );
    return ret;
}
char kgetc(void)
{
    if (kb_head == kb_tail) {
        return 0;
    }
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}
void kflush(void)
{
    kb_tail = kb_head;
}
void irqhandler(int irq_no)
{
    if (irq_no == 32) {
    }
    else if (irq_no == 33) {
        uint8_t scancode = inb(0x60);
        if (scancode == 0xE0) {
            e0_prefix = 1;
        }
        else if (e0_prefix) {
            e0_prefix = 0;
        }
        else if (scancode == 0x2A) {
            shift_pressed = 1;
        }
        else if (scancode == 0xAA) {
            shift_pressed = 0;
        }
        else if (scancode == 0x36) {
            shift_pressed = 1;
        }
        else if (scancode == 0xB6) {
            shift_pressed = 0;
        }
        else if (!(scancode & 0x80)) {
            if (scancode < sizeof(scancode_ascii)) {
                char c = scancode_ascii[scancode];
                if (c) {
                    if (shift_pressed &&
                        c >= 'a' &&
                        c <= 'z') {
                        c = (char)(c - 'a' + 'A');
                    }
                    else if (shift_pressed) {
                    switch (c) {
                    case '1': c = '!'; break;
                    case '2': c = '@'; break;
                    case '3': c = '#'; break;
                    case '4': c = '$'; break;
                    case '5': c = '%'; break;
                    case '6': c = '^'; break;
                    case '7': c = '&'; break;
                    case '8': c = '*'; break;
                    case '9': c = '('; break;
                    case '0': c = ')'; break;
                    case '-': c = '_'; break;
                    case '=': c = '+'; break;
                    case '[': c = '{'; break;
                    case ']': c = '}'; break;
                    case ';': c = ':'; break;
                    case '\'': c = '"'; break;
                    case '`': c = '~'; break;
                    case '\\': c = '|'; break;
                    case ',': c = '<'; break;
                    case '.': c = '>'; break;
                    case '/': c = '?'; break;
                    default: break;
                }
            }
                    uint32_t next =
                        (kb_head + 1) % KB_BUF_SIZE;
                    if (next != kb_tail) {
                        kb_buf[kb_head] = c;
                        kb_head = next;
                        lapic_eoi(); 
                    }
                }
            }
        }
    }
    piceoi(irq_no - 32);
}
