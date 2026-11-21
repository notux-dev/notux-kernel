#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "numbers.h"
#include "kfile.h"
#include "input.h"
#include "ktime.h"
#define EVQ 64
#define NCLIENT 16
struct evclient {
    int used, dev;
    int head, tail;
    int clock;
    struct input_event q[EVQ];
};
static struct input_dev *devs[INPUT_MAXDEV];
static struct evclient clients[NCLIENT];
char input_chan;
extern uint64_t clock_ns(int id, int *ok);
extern char poll_chan;
int input_register(struct input_dev *d)
{
    for (int i = 0; i < INPUT_MAXDEV; i++)
        if (!devs[i])
        {
            devs[i] = d;
            d->index = i;
            d->grab = -1;
            d->nev = 0;
            return i;
        }
    return -1;
}
static void push(struct evclient *c, const struct input_event *e)
{
    int next = (c->head + 1) % EVQ;
    if (next == c->tail)
    {
        c->tail = c->head;
        struct input_event d = *e;
        d.type = EV_SYN; d.code = SYN_DROPPED; d.value = 0;
        c->q[c->head] = d;
        c->head = (c->head + 1) % EVQ;
        next = (c->head + 1) % EVQ;
        if (next == c->tail)
            return;
    }
    c->q[c->head] = *e;
    c->head = next;
}
void input_event(struct input_dev *d, uint16_t type, uint16_t code, int32_t value)
{
    int ok;
    uint64_t real = clock_ns(0, &ok), mono = clock_ns(1, &ok);
    if (type == EV_KEY && code <= KEY_MAX)
    {
        if (value) input_set_bit(d->key, code); else d->key[code >> 3] &= (uint8_t)~(1u << (code & 7));
    }
    for (int i = 0; i < NCLIENT; i++)
    {
        struct evclient *c = &clients[i];
        if (!c->used || c->dev != d->index || (d->grab >= 0 && d->grab != i))
            continue;
        uint64_t t = c->clock ? mono : real;
        struct input_event e = { (int64_t)(t / 1000000000ULL), (int64_t)(t % 1000000000ULL / 1000), type, code, value };
        push(c, &e);
    }
    if (type == EV_SYN && code == SYN_REPORT)
    {
        wakeup(&input_chan);
        wakeup(&poll_chan);
    }
}
void input_report_key(struct input_dev *d, uint16_t code, int value)
{
    if (code > KEY_MAX || !input_test_bit(d->keybit, code))
        return;
    if (input_test_bit(d->key, code) == (value != 0))
        return;
    input_event(d, EV_KEY, code, value != 0);
    d->nev++;
}
void input_report_rel(struct input_dev *d, uint16_t code, int value)
{
    if (!value || code > REL_MAX || !input_test_bit(d->relbit, code))
        return;
    input_event(d, EV_REL, code, value);
    d->nev++;
}
void input_report_abs(struct input_dev *d, uint16_t code, int value)
{
    if (code > ABS_MAX || !input_test_bit(d->absbit, code) || d->abs[code].value == value)
        return;
    d->abs[code].value = value;
    input_event(d, EV_ABS, code, value);
    d->nev++;
}
void input_sync(struct input_dev *d)
{
    if (!d->nev)
        return;
    d->nev = 0;
    input_event(d, EV_SYN, SYN_REPORT, 0);
}
int evdev_open(struct kfile *f, int minor)
{
    int idx = minor - 64;
    if (idx < 0 || idx >= INPUT_MAXDEV || !devs[idx])
        return -ENODEV;
    for (int i = 0; i < NCLIENT; i++)
        if (!clients[i].used)
        {
            clients[i].used = 1;
            clients[i].dev = idx;
            clients[i].head = clients[i].tail = 0;
            clients[i].clock = 0;
            f->obj = &clients[i];
            return 0;
        }
    return -EMFILE;
}
void evdev_close(struct kfile *f)
{
    struct evclient *c = f->obj;
    if (!c)
        return;
    if (devs[c->dev] && devs[c->dev]->grab == (int)(c - clients))
        devs[c->dev]->grab = -1;
    c->used = 0;
    f->obj = 0;
}
int evdev_poll(struct kfile *f)
{
    struct evclient *c = f->obj;
    return (c && c->head != c->tail ? 1 : 0) | 4;
}
int64_t evdev_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    struct evclient *c = f->obj;
    if (!c)
        return -ENODEV;
    if (n < sizeof(struct input_event))
        return -EINVAL;
    if (c->head == c->tail)
        return (f->flags & O_NONBLOCK) ? -EAGAIN : E_BLOCK;
    int64_t done = 0;
    while (c->head != c->tail && (uint64_t)done + sizeof(struct input_event) <= n)
    {
        if (copy_to_user(p, ubuf + (uint64_t)done, &c->q[c->tail], sizeof(struct input_event)) < 0)
            return done ? done : -EFAULT;
        c->tail = (c->tail + 1) % EVQ;
        done += sizeof(struct input_event);
    }
    return done;
}
int64_t evdev_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    (void)p; (void)f; (void)ubuf;
    return (int64_t)(n - n % sizeof(struct input_event));
}
#define IOC_SIZE(r) (((r) >> 16) & 0x3fff)
#define IOC_TYPE(r) (((r) >> 8) & 0xff)
#define IOC_NR(r) ((r) & 0xff)
static int64_t put_bits(process_t *p, uint64_t arg, uint64_t size, const uint8_t *bits, int nbytes)
{
    uint8_t z[96];
    uint64_t len = size < sizeof z ? size : sizeof z;
    for (uint64_t i = 0; i < len; i++)
        z[i] = (int)i < nbytes ? bits[i] : 0;
    if (copy_to_user(p, arg, z, len) < 0)
        return -EFAULT;
    return (int64_t)len;
}
static int64_t put_str(process_t *p, uint64_t arg, uint64_t size, const char *s)
{
    uint64_t n = 0;
    while (s[n])
        n++;
    n++;
    if (n > size)
        n = size;
    if (n && copy_to_user(p, arg, s, n) < 0)
        return -EFAULT;
    return (int64_t)n;
}
int64_t evdev_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg)
{
    struct evclient *c = f->obj;
    if (!c || IOC_TYPE(req) != 'E')
        return -ENOTTY;
    struct input_dev *d = devs[c->dev];
    if (!d)
        return -ENODEV;
    uint32_t nr = IOC_NR(req), size = IOC_SIZE(req);
    int32_t v;
    if (nr >= 0x20 && nr < 0x40)
    {
        int ev = (int)nr - 0x20;
        if (ev > EV_MAX)
            return -EINVAL;
        switch (ev)
        {
        case 0: return put_bits(p, arg, size, d->evbit, sizeof d->evbit);
        case EV_KEY: return put_bits(p, arg, size, d->keybit, sizeof d->keybit);
        case EV_REL: return put_bits(p, arg, size, d->relbit, sizeof d->relbit);
        case EV_ABS: return put_bits(p, arg, size, d->absbit, sizeof d->absbit);
        default: return put_bits(p, arg, size, 0, 0);
        }
    }
    if (nr >= 0x40 && nr < 0x80)
    {
        if (size < sizeof(struct input_absinfo))
            return -EINVAL;
        return copy_to_user(p, arg, &d->abs[nr - 0x40], sizeof(struct input_absinfo)) < 0 ? -EFAULT : 0;
    }
    switch (nr)
    {
    case 0x01:
        v = 0x010001;
        return copy_to_user(p, arg, &v, 4) < 0 ? -EFAULT : 0;
    case 0x02:
    {
        uint16_t id[4] = { d->bustype, d->vendor, d->product, d->version };
        return copy_to_user(p, arg, id, size < 8 ? size : 8) < 0 ? -EFAULT : 0;
    }
    case 0x03:
        if ((req >> 30) & 2)
        {
            int32_t rep[2] = { 250, 33 };
            return copy_to_user(p, arg, rep, 8) < 0 ? -EFAULT : 0;
        }
        return 0;
    case 0x06: return put_str(p, arg, size, d->name);
    case 0x07: return d->phys[0] ? put_str(p, arg, size, d->phys) : -ENOENT;
    case 0x08: return -ENOENT;
    case 0x09: return put_bits(p, arg, size, 0, 0);
    case 0x18: return put_bits(p, arg, size, d->key, sizeof d->key);
    case 0x19: case 0x1a: case 0x1b: return put_bits(p, arg, size, 0, 0);
    case 0x84:
        v = 0;
        return copy_to_user(p, arg, &v, 4) < 0 ? -EFAULT : 0;
    case 0x90:
        if (arg)
        {
            if (d->grab >= 0 && d->grab != (int)(c - clients))
                return -EBUSY;
            d->grab = (int)(c - clients);
        }
        else if (d->grab == (int)(c - clients))
            d->grab = -1;
        else
            return -EINVAL;
        return 0;
    case 0xa0:
        if (copy_from_user(p, &v, arg, 4) < 0)
            return -EFAULT;
        if (v == 0)
            c->clock = 0;
        else if (v == 1 || v == 7)
            c->clock = 1;
        else
            return -EINVAL;
        return 0;
    }
    return -EINVAL;
}
