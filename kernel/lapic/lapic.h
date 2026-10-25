#ifndef LAPIC_H
#define LAPIC_H
#include "../types.h"
void pic_disable(void);
void lapic_init(uintptr_t virt_base);
void lapic_eoi(void);
uint32_t lapic_get_id(void);
#endif
