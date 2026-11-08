#include "pic.h"
#include "serial.h"
#include "vga.h"
#include "vbe.h"
#include "console.h"
#include "stdint.h"
#include "lapic/lapic.h"
#include "tty.h"

uint64_t current_irq_no = 0;
static int e0_prefix = 0;
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int caps_lock = 0;
static const char scancode_ascii[] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',  '7',
    '8',  '9',   '0',  '-',  '=',  127,  '\t', 'q',
    'w',  'e',   'r',  't',  'y',  'u',  'i',  'o',
    'p',  '[',   ']',  '\r', 0,    'a',  's',  'd',
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

static void send_seq(const char *s)
{
    while (*s)
        tty_input((uint8_t)*s++);
}

static char shifted(char c)
{
    switch (c) {
    case '1': return '!';  case '2': return '@';  case '3': return '#';
    case '4': return '$';  case '5': return '%';  case '6': return '^';
    case '7': return '&';  case '8': return '*';  case '9': return '(';
    case '0': return ')';  case '-': return '_';  case '=': return '+';
    case '[': return '{';  case ']': return '}';  case ';': return ':';
    case '\'': return '"'; case '`': return '~';  case '\\': return '|';
    case ',': return '<';  case '.': return '>';  case '/': return '?';
    default: return c;
    }
}

static void extended_key(uint8_t sc)
{
    switch (sc) {
    case 0x1D: ctrl_pressed = 1; break;
    case 0x9D: ctrl_pressed = 0; break;
    case 0x48: send_seq("\x1b[A"); break;
    case 0x50: send_seq("\x1b[B"); break;
    case 0x4D: send_seq("\x1b[C"); break;
    case 0x4B: send_seq("\x1b[D"); break;
    case 0x47: send_seq("\x1b[H"); break;
    case 0x4F: send_seq("\x1b[F"); break;
    case 0x53: send_seq("\x1b[3~"); break;
    case 0x52: send_seq("\x1b[2~"); break;
    case 0x49: send_seq("\x1b[5~"); break;
    case 0x51: send_seq("\x1b[6~"); break;
    case 0x1C: tty_input('\r'); break;
    case 0x35: tty_input('/'); break;
    default: break;
    }
}

void irqhandler(int irq_no)
{
    if (irq_no == 33) {
        uint8_t scancode = inb(0x60);
        if (scancode == 0xE0) {
            e0_prefix = 1;
        }
        else if (e0_prefix) {
            e0_prefix = 0;
            extended_key(scancode);
        }
        else if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
        }
        else if (scancode == 0xAA || scancode == 0xB6) {
            shift_pressed = 0;
        }
        else if (scancode == 0x1D) {
            ctrl_pressed = 1;
        }
        else if (scancode == 0x9D) {
            ctrl_pressed = 0;
        }
        else if (scancode == 0x3A) {
            caps_lock = !caps_lock;
        }
        else if (!(scancode & 0x80)) {
            if (scancode < sizeof(scancode_ascii)) {
                char c = scancode_ascii[scancode];
                if (c) {
                    int letter = c >= 'a' && c <= 'z';
                    if (letter && (shift_pressed ^ caps_lock))
                        c = (char)(c - 'a' + 'A');
                    else if (!letter && shift_pressed)
                        c = shifted(c);
                    if (ctrl_pressed) {
                        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
                        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
                        else if (c == '[' || c == '{') c = 27;
                        else if (c == '\\' || c == '|') c = 28;
                        else if (c == ']' || c == '}') c = 29;
                        else if (c == ' ' || c == '2' || c == '@') c = 0;
                        else if (c == '6' || c == '^') c = 30;
                        else if (c == '-' || c == '_') c = 31;
                        else if (c == '?' || c == '/') c = 127;
                    }
                    tty_input((uint8_t)c);
                    lapic_eoi();
                }
            }
        }
    }
    else if (irq_no == 36) {
        
        while (serrx_ready())
            tty_input(serrx_get());
    }
    piceoi(irq_no - 32);
}
