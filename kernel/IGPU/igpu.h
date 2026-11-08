#ifndef IGPU_H
#define IGPU_H
#include "stdint.h"
extern uint64_t gpu_mmio_base;
int igpu_pci_init(void);
#endif
