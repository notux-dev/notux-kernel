#include "ktime.h"
#include "process.h"
static inline void outb(uint16_t port, uint8_t v) { asm volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t r; asm volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r; }

volatile uint64_t ktime_ticks;

void timer_tick(void)
{
    ktime_ticks++;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_t *p = &processes[i];
        if (p->alarm_tick && ktime_ticks >= p->alarm_tick)
        {
            p->alarm_tick = p->alarm_interval ? ktime_ticks + p->alarm_interval : 0;
            signal_post(p, 14);
        }
        if (p->state == PROCESS_STATE_WAITING && p->wake_tick &&
            ktime_ticks >= p->wake_tick)
        {
            p->wake_tick = 0;
            p->wchan = 0;
            p->state = PROCESS_STATE_READY;
        }
    }
}

uint64_t ktime_monotonic_ns(void)
{
    return ktime_ticks * (1000000000ULL / HZ);
}

static uint8_t cmos(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static uint64_t rtc_epoch(void)
{
    uint8_t s, mi, h, d, mo, y, c, s2, mi2, h2, d2, mo2, y2;
    for (int tries = 0; tries < 5; tries++)
    {
        while (cmos(0x0A) & 0x80) ;
        s = cmos(0); mi = cmos(2); h = cmos(4); d = cmos(7); mo = cmos(8); y = cmos(9);
        while (cmos(0x0A) & 0x80) ;
        s2 = cmos(0); mi2 = cmos(2); h2 = cmos(4); d2 = cmos(7); mo2 = cmos(8); y2 = cmos(9);
        if (s == s2 && mi == mi2 && h == h2 && d == d2 && mo == mo2 && y == y2)
            break;
    }
    c = cmos(0x32);
    uint8_t b = cmos(0x0B);
    if (!(b & 4))
    {
#define BCD(x) (uint8_t)(((x) & 0x0F) + ((x) >> 4) * 10)
        int pm = h & 0x80;
        s = BCD(s); mi = BCD(mi); h = BCD(h & 0x7F); d = BCD(d); mo = BCD(mo);
        y = BCD(y); c = BCD(c);
        if (!(b & 2) && pm) h = (uint8_t)((h % 12) + 12);
    }
    else if (!(b & 2) && (h & 0x80))
        h = (uint8_t)(((h & 0x7F) % 12) + 12);
    int year = (c ? c : 20) * 100 + y;
    if (mo < 1 || mo > 12) mo = 1;
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int64_t days = (int64_t)(year - 1970) * 365 + (year - 1969) / 4 - (year - 1901) / 100 +
                   (year - 1601) / 400;
    days += cum[mo - 1] + (d - 1);
    if (mo > 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        days++;
    return (uint64_t)days * 86400 + h * 3600 + mi * 60 + s;
}

uint64_t ktime_realtime_ns(void)
{
    static uint64_t boot_epoch_ns;
    if (!boot_epoch_ns)
        boot_epoch_ns = rtc_epoch() * 1000000000ULL - ktime_monotonic_ns();
    return boot_epoch_ns + ktime_monotonic_ns();
}
