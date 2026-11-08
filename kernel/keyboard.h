#ifndef KEYBOARD_H
#define KEYBOARD_H
extern uint64_t current_irq_no;
void irqhandler(int irq_no);
char kgetc(void);
int kbd_has_char(void);
void kflush(void);
#endif
