#include "syscalls.h"
#include "kfile.h"
#include "process.h"
#include "uaccess.h"
#include "console.h"
#include "vbe.h"
#include "vmm.h"
#include "numbers.h"
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOGETCMAP 0x4604
#define FBIOPUTCMAP 0x4605
#define FBIOPAN_DISPLAY 0x4606
#define FBIOBLANK 0x4611
#define FB_ACTIVATE_TEST 0x10
struct fb_bitfield { uint32_t offset, length, msb_right; };
struct fb_var {
    uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale;
    struct fb_bitfield red, green, blue, transp;
    uint32_t nonstd, activate, height, width, accel_flags;
    uint32_t pixclock, left_margin, right_margin, upper_margin, lower_margin, hsync_len, vsync_len, sync, vmode, rotate,
             colorspace;
    uint32_t reserved[4];
};
struct fb_fix {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len, type, type_aux, visual;
    uint16_t xpanstep, ypanstep, ywrapstep, pad0;
    uint32_t line_length, pad1;
    uint64_t mmio_start;
    uint32_t mmio_len, accel;
    uint16_t capabilities, reserved[2], pad2;
};
uint32_t fb_size_bytes(void)
{
    uint32_t v = vbe_vram_bytes(), cur = vbe_pitch() * vbe_height();
    return v > cur ? v : cur;
}
uint64_t fb_phys_base(void) { return vbeaddr(); }
static void fill_var(struct fb_var *v)
{
    uint8_t *z = (uint8_t *)v;
    for (unsigned i = 0; i < sizeof *v; i++)
        z[i] = 0;
    v->xres = v->xres_virtual = vbe_width();
    v->yres = v->yres_virtual = vbe_height();
    v->bits_per_pixel = 32;
    v->red = (struct fb_bitfield){16, 8, 0};
    v->green = (struct fb_bitfield){8, 8, 0};
    v->blue = (struct fb_bitfield){0, 8, 0};
    v->transp = (struct fb_bitfield){24, 8, 0};
    v->height = (uint32_t)((uint64_t)vbe_height() * 254 / 960);
    v->width = (uint32_t)((uint64_t)vbe_width() * 254 / 960);
    v->vmode = 0;
}
int64_t fb_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg)
{
    (void)f;
    switch (req)
    {
    case FBIOGET_VSCREENINFO:
    {
        struct fb_var v;
        fill_var(&v);
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case FBIOPUT_VSCREENINFO:
    {
        struct fb_var v;
        if (copy_from_user(p, &v, arg, sizeof v) < 0)
            return -EFAULT;
        if (v.bits_per_pixel != 32 && v.bits_per_pixel != 0)
            return -EINVAL;
        uint32_t w = v.xres ? v.xres : vbe_width(), h = v.yres ? v.yres : vbe_height();
        if (w != vbe_width() || h != vbe_height())
        {
            if (!vbe_can_set_mode())
                return -EINVAL;
            if (w < 320 || h < 200 || w > VBE_MAX_W || h > VBE_MAX_H || (w & 7) || (uint64_t)w * h * 4 > vbe_vram_bytes())
                return -EINVAL;
            if (!(v.activate & FB_ACTIVATE_TEST) && !display_set_mode(w, h))
                return -EINVAL;
        }
        if (v.activate & FB_ACTIVATE_TEST)
            return 0;
        fill_var(&v);
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case FBIOGET_FSCREENINFO:
    {
        struct fb_fix x;
        uint8_t *z = (uint8_t *)&x;
        for (unsigned i = 0; i < sizeof x; i++)
            z[i] = 0;
        const char *id = "notux fb";
        for (int i = 0; id[i]; i++)
            x.id[i] = id[i];
        x.smem_start = fb_phys_base();
        x.smem_len = fb_size_bytes();
        x.type = 0;
        x.visual = 2;
        x.line_length = vbe_pitch();
        return copy_to_user(p, arg, &x, sizeof x) < 0 ? -EFAULT : 0;
    }
    case FBIOPAN_DISPLAY:
    case FBIOBLANK:
        return 0;
    case FBIOGETCMAP:
    case FBIOPUTCMAP:
        return 0;
    }
    return -ENOTTY;
}
int64_t fb_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    uint64_t size = fb_size_bytes();
    if (f->off >= size)
        return 0;
    if (n > size - f->off)
        n = size - f->off;
    uint64_t done = 0;
    while (done < n)
    {
        uint64_t c = n - done > 4096 ? 4096 : n - done;
        if (copy_to_user(p, ubuf + done, (const void *)(uintptr_t)(fb_phys_base() + f->off), c) < 0)
            return done ? (int64_t)done : -EFAULT;
        f->off += c;
        done += c;
    }
    return (int64_t)done;
}
int64_t fb_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    uint64_t size = fb_size_bytes();
    if (f->off >= size)
        return n ? -ENOSPC : 0;
    if (n > size - f->off)
        n = size - f->off;
    uint64_t done = 0;
    while (done < n)
    {
        uint64_t c = n - done > 4096 ? 4096 : n - done;
        if (copy_from_user(p, (void *)(uintptr_t)(fb_phys_base() + f->off), ubuf + done, c) < 0)
            return done ? (int64_t)done : -EFAULT;
        f->off += c;
        done += c;
    }
    return (int64_t)done;
}
int64_t fb_seek(struct kfile *f, int64_t off, int whence)
{
    int64_t base = whence == 0 ? 0 : whence == 1 ? (int64_t)f->off : whence == 2 ? (int64_t)fb_size_bytes() : -1;
    if (base < 0 || base + off < 0)
        return -EINVAL;
    f->off = (uint64_t)(base + off);
    return (int64_t)f->off;
}
void fb_close(struct kfile *f)
{
    if (f->ev_flag & 2)
        console_gfx_hold(-1);
    f->ev_flag &= ~2;
}
