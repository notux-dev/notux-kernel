#ifndef KTIME_H
#define KTIME_H
#include "stdint.h"
#define HZ 100
extern volatile uint64_t ktime_ticks;
void timer_tick(void);
uint64_t ktime_monotonic_ns(void);
uint64_t ktime_realtime_ns(void);
#endif
