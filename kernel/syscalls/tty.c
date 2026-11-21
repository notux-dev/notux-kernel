#include "syscalls.h"
#include "tty.h"
#include "console.h"
#include "uaccess.h"
#include "ktime.h"
struct ktermios {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t c_line;
    uint8_t c_cc[19];
};
#define IGNBRK 0x1
#define ISTRIP 0x20
#define INLCR 0x40
#define IGNCR 0x80
#define ICRNL 0x100
#define OPOST 0x1
#define ONLCR 0x4
#define OCRNL 0x8
#define ISIG 0x1
#define ICANON 0x2
#define ECHO 0x8
#define ECHOE 0x10
#define ECHOK 0x20
#define ECHONL 0x40
#define NOFLSH 0x80
#define ECHOCTL 0x200
#define ECHOKE 0x800
#define IEXTEN 0x8000
enum { VINTR, VQUIT, VERASE, VKILL, VEOF, VTIME, VMIN, VSWTC, VSTART, VSTOP,
       VSUSP, VEOL, VREPRINT, VDISCARD, VWERASE, VLNEXT, VEOL2 };
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCSBRK 0x5409
#define TCXONC 0x540A
#define TCFLSH 0x540B
#define TIOCEXCL 0x540C
#define TIOCNXCL 0x540D
#define TIOCSCTTY 0x540E
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCOUTQ 0x5411
#define TIOCSTI 0x5412
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCNOTTY 0x5422
#define TIOCGSID 0x5429
#define FIONREAD_ 0x541B
#define RQ 1024
#define LINE 256
#define PTYS 16
#define MRING 4096
struct tty {
    struct ktermios tio;
    struct winsz { uint16_t row, col, xpix, ypix; } ws;
    uint32_t fg_pgrp, session;
    uint8_t rq[RQ], rqm[RQ];
    uint32_t rhead, rtail, nlines;
    int eof_pending;
    uint8_t line[LINE];
    int linelen;
    int lnext;
    char chan;
    int pty;
    int used, locked;
    int master_open, slave_refs, slave_seen;
    uint8_t mring[MRING];
    uint32_t mh, mt;
    char mchan;
    char wchan;
    uint32_t owner;
};
static struct tty con_tty;
static struct tty ptys[PTYS];
char tty_chan;
static struct tty *ttyof(struct kfile *f)
{
    return (f && f->obj && f->type == FT_CONSOLE) ? (struct tty *)f->obj : &con_tty;
}
static void termios_defaults(struct tty *t)
{
    t->tio.c_iflag = ICRNL | 0x400;
    t->tio.c_oflag = OPOST | ONLCR;
    t->tio.c_cflag = 0x4BF;
    t->tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
    t->tio.c_cc[VINTR] = 3; t->tio.c_cc[VQUIT] = 28; t->tio.c_cc[VERASE] = 127;
    t->tio.c_cc[VKILL] = 21; t->tio.c_cc[VEOF] = 4; t->tio.c_cc[VTIME] = 0; t->tio.c_cc[VMIN] = 1;
    t->tio.c_cc[VSTART] = 17; t->tio.c_cc[VSTOP] = 19; t->tio.c_cc[VSUSP] = 26;
    t->tio.c_cc[VREPRINT] = 18; t->tio.c_cc[VDISCARD] = 15; t->tio.c_cc[VWERASE] = 23;
    t->tio.c_cc[VLNEXT] = 22;
}
void tty_init(void)
{
    termios_defaults(&con_tty);
    con_tty.fg_pgrp = 1;
    con_tty.session = 1;
    con_tty.ws.row = (uint16_t)console_rows();
    con_tty.ws.col = (uint16_t)console_cols();
}
extern void serputc(char c);
static int esc_state;
static int csi_p[4], csi_n;
static int csi_priv;
static int save_x, save_y;
static void csi_done(char f)
{
    int a = csi_p[0], b = csi_p[1];
    int x = console_get_col(), y = console_get_row();
    int n1 = a > 0 ? a : 1;
    switch (f)
    {
    case 'A': console_set_cursor(x, y - n1); break;
    case 'B': console_set_cursor(x, y + n1); break;
    case 'C': console_set_cursor(x + n1, y); break;
    case 'D': console_set_cursor(x - n1, y); break;
    case 'E': console_set_cursor(0, y + n1); break;
    case 'F': console_set_cursor(0, y - n1); break;
    case 'G': console_set_cursor(n1 - 1, y); break;
    case 'd': console_set_cursor(x, n1 - 1); break;
    case 'H': case 'f':
        console_set_cursor((b > 0 ? b : 1) - 1, (a > 0 ? a : 1) - 1);
        break;
    case 'J': console_erase_screen(a); break;
    case 'K': console_erase_line(a); break;
    case 's': save_x = x; save_y = y; break;
    case 'u': console_set_cursor(save_x, save_y); break;
    default: break;
    }
}
static void screen_byte(uint8_t c)
{
    if (esc_state == 1)
    {
        if (c == '[')
        {
            esc_state = 2; csi_n = 0; csi_priv = 0;
            csi_p[0] = csi_p[1] = csi_p[2] = csi_p[3] = 0;
            return;
        }
        esc_state = 0;
        if (c == '7') { save_x = console_get_col(); save_y = console_get_row(); }
        else if (c == '8') console_set_cursor(save_x, save_y);
        else if (c == 'c') { console_erase_screen(2); console_set_cursor(0, 0); }
        return;
    }
    if (esc_state == 2)
    {
        if (c >= '0' && c <= '9')
            csi_p[csi_n < 4 ? csi_n : 3] = csi_p[csi_n < 4 ? csi_n : 3] * 10 + (c - '0');
        else if (c == ';')
        {
            if (csi_n < 3) csi_n++;
        }
        else if (c == '?' || c == '>' || c == '=' || c == ' ' || c == '!')
            csi_priv = 1;
        else
        {
            esc_state = 0;
            if (!csi_priv)
                csi_done((char)c);
        }
        return;
    }
    switch (c)
    {
    case 27: esc_state = 1; break;
    case '\r': console_cr(); break;
    case '\n': console_lf(); break;
    case '\b': console_bs(); break;
    case '\t':
    {
        int x = (console_get_col() + 8) & ~7;
        console_set_cursor(x, console_get_row());
        break;
    }
    case 7: break;
    default:
        if (c >= 0x20 && c < 0x7f)
            console_putc((char)c);
        break;
    }
}
static void wake_master(struct tty *t)
{
    wakeup(&t->mchan);
    wakeup(&poll_chan);
}
static uint32_t mring_free(struct tty *t)
{
    return MRING - 1 - (t->mh + MRING - t->mt) % MRING;
}
static void out_raw(struct tty *t, uint8_t c)
{
    if (t->pty)
    {
        if (mring_free(t))
        {
            t->mring[t->mh] = c;
            t->mh = (t->mh + 1) % MRING;
        }
        return;
    }
    serputc((char)c);
    screen_byte(c);
}
static void out_byte(struct tty *t, uint8_t c)
{
    if (t->tio.c_oflag & OPOST)
    {
        if (c == '\n' && (t->tio.c_oflag & ONLCR))
            out_raw(t, '\r');
        else if (c == '\r' && (t->tio.c_oflag & OCRNL))
            c = '\n';
    }
    out_raw(t, c);
}
void tty_write_kernel(const char *s, uint64_t n)
{
    console_cursor(0);
    for (uint64_t i = 0; i < n; i++)
        out_byte(&con_tty, (uint8_t)s[i]);
    console_cursor(1);
}
int64_t tty_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    struct tty *t = ttyof(f);
    uint8_t buf[256];
    int64_t total = 0;
    if (t->pty)
    {
        if (!t->master_open)
            return -EIO;
        while ((uint64_t)total < n)
        {
            if (mring_free(t) < 2)
                break;
            uint8_t c;
            if (copy_from_user(p, &c, ubuf + total, 1) < 0)
                return total ? total : -EFAULT;
            out_byte(t, c);
            total++;
        }
        if (total)
            wake_master(t);
        if (!total && n)
        {
            if (f->flags & O_NONBLOCK)
                return -EAGAIN;
            return E_BLOCK;
        }
        return total;
    }
    console_cursor(0);
    while ((uint64_t)total < n)
    {
        uint64_t c = n - total;
        if (c > sizeof buf)
            c = sizeof buf;
        if (copy_from_user(p, buf, ubuf + total, c) < 0)
        {
            console_cursor(1);
            return total ? total : -EFAULT;
        }
        for (uint64_t i = 0; i < c; i++)
            out_byte(t, buf[i]);
        total += c;
    }
    console_cursor(1);
    return total;
}
static void echo_byte(struct tty *t, uint8_t c)
{
    if (!(t->tio.c_lflag & ECHO))
        return;
    if (c < 0x20 && c != '\n' && c != '\t' && (t->tio.c_lflag & ECHOCTL))
    {
        out_byte(t, '^');
        out_byte(t, (uint8_t)(c + '@'));
    }
    else if (c == 0x7f && (t->tio.c_lflag & ECHOCTL))
    {
        out_byte(t, '^');
        out_byte(t, '?');
    }
    else
        out_byte(t, c);
}
static int echo_width(struct tty *t, uint8_t c)
{
    return ((c < 0x20 && c != '\t' && c != '\n') || c == 0x7f) && (t->tio.c_lflag & ECHOCTL) ? 2 : 1;
}
static void erase_one(struct tty *t)
{
    if (t->linelen == 0)
        return;
    uint8_t c = t->line[--t->linelen];
    if ((t->tio.c_lflag & ECHO) && (t->tio.c_lflag & ECHOE))
        for (int i = echo_width(t, c); i > 0; i--)
        {
            out_byte(t, '\b'); out_byte(t, ' '); out_byte(t, '\b');
        }
}
static void rq_push(struct tty *t, uint8_t c, int mark)
{
    uint32_t next = (t->rhead + 1) % RQ;
    if (next == t->rtail)
        return;
    t->rq[t->rhead] = c;
    t->rqm[t->rhead] = (uint8_t)mark;
    t->rhead = next;
    if (mark)
        t->nlines++;
}
static void commit_line(struct tty *t, int with_newline)
{
    for (int i = 0; i < t->linelen; i++)
        rq_push(t, t->line[i], 0);
    if (with_newline)
        rq_push(t, '\n', 1);
    else if (t->linelen)
    {
        t->rqm[(t->rhead + RQ - 1) % RQ] = 1;
        t->nlines++;
    }
    else
        t->eof_pending = 1;
    t->linelen = 0;
}
static void signal_fg(struct tty *t, int sig)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_t *pr = &processes[i];
        if (pr->state != PROCESS_STATE_TERMINATED && pr->state != PROCESS_STATE_ZOMBIE &&
            pr->pid && pr->pgid == t->fg_pgrp)
            signal_post(pr, sig);
    }
}
static void flush_input(struct tty *t)
{
    t->rhead = t->rtail = 0;
    t->nlines = 0;
    t->linelen = 0;
    t->eof_pending = 0;
}
static void ld_input(struct tty *t, uint8_t c)
{
    if (!t->pty)
        console_cursor(0);
    if (t->tio.c_iflag & ISTRIP)
        c &= 0x7f;
    if (c == '\r')
    {
        if (t->tio.c_iflag & IGNCR)
            goto out;
        if (t->tio.c_iflag & ICRNL)
            c = '\n';
    }
    else if (c == '\n' && (t->tio.c_iflag & INLCR))
        c = '\r';
    if (t->lnext)
    {
        t->lnext = 0;
        goto store;
    }
    if (t->tio.c_lflag & ISIG)
    {
        int sig = 0;
        if (c == t->tio.c_cc[VINTR]) sig = 2;
        else if (c == t->tio.c_cc[VQUIT]) sig = 3;
        else if (c == t->tio.c_cc[VSUSP]) sig = 20;
        if (sig)
        {
            if (!(t->tio.c_lflag & NOFLSH))
                flush_input(t);
            echo_byte(t, c);
            if (t->tio.c_lflag & ECHO)
                out_byte(t, '\n');
            signal_fg(t, sig);
            goto wake;
        }
    }
    if ((t->tio.c_lflag & IEXTEN) && c == t->tio.c_cc[VLNEXT] && (t->tio.c_lflag & ICANON))
    {
        t->lnext = 1;
        goto out;
    }
store:
    if (t->tio.c_lflag & ICANON)
    {
        if (c == t->tio.c_cc[VERASE] || c == 0x08)
        {
            erase_one(t);
            goto out;
        }
        if (c == t->tio.c_cc[VKILL])
        {
            while (t->linelen)
                erase_one(t);
            if ((t->tio.c_lflag & ECHO) && (t->tio.c_lflag & ECHOK) && !(t->tio.c_lflag & ECHOE))
                out_byte(t, '\n');
            goto out;
        }
        if (c == t->tio.c_cc[VWERASE])
        {
            while (t->linelen && t->line[t->linelen - 1] == ' ')
                erase_one(t);
            while (t->linelen && t->line[t->linelen - 1] != ' ')
                erase_one(t);
            goto out;
        }
        if (c == t->tio.c_cc[VEOF])
        {
            commit_line(t, 0);
            goto wake;
        }
        if (c == '\n')
        {
            if (t->tio.c_lflag & (ECHO | ECHONL))
                out_byte(t, '\n');
            commit_line(t, 1);
            goto wake;
        }
        if (t->linelen < LINE - 1)
        {
            t->line[t->linelen++] = c;
            echo_byte(t, c);
        }
        goto out;
    }
    rq_push(t, c, 0);
    if (t->tio.c_lflag & ECHO)
        echo_byte(t, c);
wake:
    wakeup(&t->chan);
    if (t == &con_tty)
        wakeup(&tty_chan);
    wakeup(&poll_chan);
out:
    if (!t->pty)
        console_cursor(1);
    if (t->pty)
        wake_master(t);
}
void tty_input(uint8_t c) { ld_input(&con_tty, c); }
static int canon(struct tty *t) { return (t->tio.c_lflag & ICANON) != 0; }
static uint32_t rq_count(struct tty *t)
{
    return (t->rhead + RQ - t->rtail) % RQ;
}
int tty_poll(struct kfile *f)
{
    struct tty *t = ttyof(f);
    if (canon(t))
        return (t->nlines || t->eof_pending) ? 1 : 0;
    return rq_count(t) ? 1 : 0;
}
int tty_nread(struct kfile *f)
{
    struct tty *t = ttyof(f);
    if (canon(t))
    {
        uint32_t n = 0;
        for (uint32_t i = t->rtail; i != t->rhead; i = (i + 1) % RQ)
        {
            n++;
            if (t->rqm[i])
                break;
        }
        return t->nlines ? (int)n : 0;
    }
    return (int)rq_count(t);
}
char kgetc(void)
{
    struct tty *t = &con_tty;
    if (t->rtail == t->rhead)
        return 0;
    char c = (char)t->rq[t->rtail];
    if (t->rqm[t->rtail] && t->nlines)
        t->nlines--;
    t->rtail = (t->rtail + 1) % RQ;
    return c;
}
int kbd_has_char(void) { return con_tty.rtail != con_tty.rhead; }
void kflush(void) { flush_input(&con_tty); }
int64_t tty_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n, void **chan)
{
    struct tty *t = ttyof(f);
    int nonblock = (f->flags & O_NONBLOCK) != 0;
    if (t->pty && !t->master_open && t->rhead == t->rtail && !t->nlines)
        return 0;
    if (canon(t))
    {
        if (!t->nlines && !t->eof_pending)
        {
            if (nonblock)
                return -EAGAIN;
            *chan = &t->chan;
            return E_BLOCK;
        }
        if (!t->nlines && t->eof_pending)
        {
            t->eof_pending = 0;
            return 0;
        }
    }
    else
    {
        uint32_t need = t->tio.c_cc[VMIN] ? t->tio.c_cc[VMIN] : 0;
        if (rq_count(t) == 0 || rq_count(t) < need)
        {
            if (need == 0)
                return nonblock ? -EAGAIN : 0;
            if (nonblock)
                return -EAGAIN;
            *chan = &t->chan;
            return E_BLOCK;
        }
    }
    uint8_t tmp[128];
    int64_t total = 0;
    while ((uint64_t)total < n && t->rtail != t->rhead)
    {
        int k = 0;
        int ended = 0;
        while (k < (int)sizeof tmp && (uint64_t)(total + k) < n && t->rtail != t->rhead)
        {
            tmp[k++] = t->rq[t->rtail];
            if (t->rqm[t->rtail])
            {
                if (canon(t) && t->nlines)
                    t->nlines--;
                ended = 1;
                t->rtail = (t->rtail + 1) % RQ;
                break;
            }
            t->rtail = (t->rtail + 1) % RQ;
        }
        if (copy_to_user(p, ubuf + total, tmp, (uint64_t)k) < 0)
            return total ? total : -EFAULT;
        total += k;
        if (ended && canon(t))
            break;
        if (!canon(t) && ended && total >= 1 && rq_count(t) == 0)
            break;
    }
    return total;
}
#define KDSETMODE 0x4B3A
#define KDGETMODE 0x4B3B
#define KDGKBTYPE 0x4B33
#define KDGETLED 0x4B31
#define KDSETLED 0x4B32
#define KDGKBLED 0x4B64
#define KDSKBLED 0x4B65
#define KDGKBMODE 0x4B44
#define KDSKBMODE 0x4B45
#define KDKBDREP 0x4B52
#define KDENABIO 0x4B34
#define KDDISABIO 0x4B35
#define KIOCSOUND 0x4B2F
#define KDMKTONE 0x4B30
#define VT_OPENQRY 0x5600
#define VT_GETMODE 0x5601
#define VT_SETMODE 0x5602
#define VT_GETSTATE 0x5603
#define VT_SENDSIG 0x5604
#define VT_RELDISP 0x5605
#define VT_ACTIVATE 0x5606
#define VT_WAITACTIVE 0x5607
#define VT_DISALLOCATE 0x5608
#define VT_RESIZE 0x5609
#define VT_RESIZEX 0x560A
#define VT_LOCKSWITCH 0x560B
#define VT_UNLOCKSWITCH 0x560C
#define K_UNICODE 3
#define K_XLATE 1
struct vt_mode_ { char mode, waitv; short relsig, acqsig, frsig; };
struct vt_stat_ { unsigned short v_active, v_signal, v_state; };
static int kbmode = K_UNICODE;
static int kd_gfx = 0;
static struct kfile *vt_owner = 0;
static struct vt_mode_ vtm;
int tty_kbd_on(void) { return kbmode == K_XLATE || kbmode == K_UNICODE; }
void tty_vt_release(struct kfile *f)
{
    if (f != vt_owner)
        return;
    vt_owner = 0;
    kbmode = K_UNICODE;
    if (kd_gfx)
    {
        kd_gfx = 0;
        console_gfx_hold(-1);
    }
}
static int64_t vt_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg, int *handled)
{
    *handled = 1;
    switch (req)
    {
    case KDSETMODE:
        if (arg > 1)
            return -EINVAL;
        if (arg == 1 && !kd_gfx)
        {
            kd_gfx = 1;
            vt_owner = f; f->ev_flag |= 4;
            console_gfx_hold(1);
        }
        else if (arg == 0 && kd_gfx)
        {
            kd_gfx = 0;
            console_gfx_hold(-1);
        }
        return 0;
    case KDGETMODE:
    {
        int v = kd_gfx;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case KDGKBMODE:
    {
        int v = kbmode;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case KDSKBMODE:
        if (arg > 4)
            return -EINVAL;
        kbmode = (int)arg;
        if (kbmode != K_UNICODE)
        {
            vt_owner = f; f->ev_flag |= 4;
        }
        return 0;
    case KDGKBTYPE:
    {
        char v = 2;
        return copy_to_user(p, arg, &v, 1) < 0 ? -EFAULT : 0;
    }
    case KDGETLED: case KDGKBLED:
    {
        char v = 0;
        return copy_to_user(p, arg, &v, 1) < 0 ? -EFAULT : 0;
    }
    case KDSETLED: case KDSKBLED: case KDKBDREP: case KDENABIO: case KDDISABIO: case KIOCSOUND: case KDMKTONE:
        return 0;
    case VT_OPENQRY:
    {
        int v = 1;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case VT_GETMODE:
        return copy_to_user(p, arg, &vtm, sizeof vtm) < 0 ? -EFAULT : 0;
    case VT_SETMODE:
        return copy_from_user(p, &vtm, arg, sizeof vtm) < 0 ? -EFAULT : 0;
    case VT_GETSTATE:
    {
        struct vt_stat_ st = { 1, 0, 2 };
        return copy_to_user(p, arg, &st, sizeof st) < 0 ? -EFAULT : 0;
    }
    case VT_ACTIVATE: case VT_WAITACTIVE:
        return (arg == 1 || arg == 0) ? 0 : -ENXIO;
    case VT_SENDSIG: case VT_RELDISP: case VT_DISALLOCATE: case VT_RESIZE: case VT_RESIZEX:
    case VT_LOCKSWITCH: case VT_UNLOCKSWITCH:
        return 0;
    }
    *handled = 0;
    return 0;
}
#define TIOCGPTN 0x80045430
#define TIOCSPTLCK 0x40045431
#define TIOCGPTLCK 0x80045439
#define TIOCPKT 0x5420
static int64_t tty_common_ioctl(process_t *p, struct tty *t, uint64_t req, uint64_t arg)
{
    switch (req)
    {
    case TCGETS:
        return copy_to_user(p, arg, &t->tio, sizeof t->tio) < 0 ? -EFAULT : 0;
    case TCSETS: case TCSETSW: case TCSETSF:
    {
        struct ktermios n;
        if (copy_from_user(p, &n, arg, sizeof n) < 0)
            return -EFAULT;
        if (req == TCSETSF)
            flush_input(t);
        int was_canon = canon(t);
        t->tio = n;
        t->nlines = 0;
        for (uint32_t i = t->rtail; i != t->rhead; i = (i + 1) % RQ)
            if (t->rqm[i])
                t->nlines++;
        if (was_canon && !canon(t))
        {
            for (int i = 0; i < t->linelen; i++)
                rq_push(t, t->line[i], 0);
            t->linelen = 0;
        }
        wakeup(&t->chan);
        wakeup(&poll_chan);
        return 0;
    }
    case TCFLSH:
        if (arg == 0 || arg == 2)
            flush_input(t);
        if (t->pty && (arg == 1 || arg == 2))
        {
            t->mh = t->mt = 0;
            wakeup(&t->wchan);
        }
        return 0;
    case TCSBRK: case TCXONC: case TIOCEXCL: case TIOCNXCL: case TIOCPKT:
        return 0;
    case TIOCGWINSZ:
        return copy_to_user(p, arg, &t->ws, sizeof t->ws) < 0 ? -EFAULT : 0;
    case TIOCSWINSZ:
    {
        struct winsz n;
        if (copy_from_user(p, &n, arg, sizeof n) < 0)
            return -EFAULT;
        int changed = n.row != t->ws.row || n.col != t->ws.col;
        t->ws = n;
        if (changed && t->pty)
            signal_fg(t, 28);
        return 0;
    }
    case TIOCGPGRP:
    {
        int v = (int)t->fg_pgrp;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case TIOCSPGRP:
    {
        int v;
        if (copy_from_user(p, &v, arg, sizeof v) < 0)
            return -EFAULT;
        if (v <= 0)
            return -EINVAL;
        t->fg_pgrp = (uint32_t)v;
        return 0;
    }
    case TIOCGSID:
    {
        int v = (int)t->session;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case TIOCSCTTY:
        t->session = p->sid;
        t->fg_pgrp = p->pgid;
        return 0;
    case TIOCNOTTY:
        return 0;
    case TIOCOUTQ:
    {
        int v = 0;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case TIOCSTI:
    {
        uint8_t c;
        if (copy_from_user(p, &c, arg, 1) < 0)
            return -EFAULT;
        ld_input(t, c);
        return 0;
    }
    }
    return -ENOTTY;
}
int64_t tty_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg)
{
    struct tty *t = ttyof(f);
    if (!t->pty)
    {
        int h;
        int64_t vr = vt_ioctl(p, f, req, arg, &h);
        if (h)
            return vr;
    }
    if (req == FIONREAD_)
    {
        int v = tty_nread(f);
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    return tty_common_ioctl(p, t, req, arg);
}
void *pty_master_chan(struct kfile *f)
{
    struct tty *t = f->obj;
    return t ? (void *)&t->mchan : (void *)f;
}
void *tty_wait_chan(struct kfile *f, int wr)
{
    struct tty *t = ttyof(f);
    return wr ? (void *)&t->wchan : (void *)&t->chan;
}
static int pty_index(struct tty *t) { return t->pty - 1; }
static struct tty *pty_alloc(process_t *p)
{
    for (int i = 0; i < PTYS; i++)
    {
        struct tty *t = &ptys[i];
        if (t->used)
            continue;
        uint8_t *z = (uint8_t *)t;
        for (unsigned k = 0; k < sizeof *t; k++)
            z[k] = 0;
        t->used = 1;
        t->pty = i + 1;
        t->locked = 1;
        t->master_open = 1;
        termios_defaults(t);
        t->ws.row = 24;
        t->ws.col = 80;
        t->owner = p->cred.uid;
        return t;
    }
    return 0;
}
static void pty_release_check(struct tty *t)
{
    if (!t->master_open && t->slave_refs == 0)
        t->used = 0;
}
int pty_open_master(struct kfile *f)
{
    struct tty *t = pty_alloc(get_current_process());
    if (!t)
        return -ENOSPC;
    f->obj = t;
    f->pkind = (5 << 8) | 2;
    return 0;
}
static int pts_parse(const char *path, int *idx)
{
    const char *pre = "/dev/pts";
    int i = 0;
    for (; pre[i]; i++)
        if (path[i] != pre[i])
            return PTS_NOTMINE;
    if (path[i] == 0)
    {
        *idx = -1;
        return 0;
    }
    if (path[i] != '/')
        return PTS_NOTMINE;
    i++;
    if (path[i] < '0' || path[i] > '9')
        return -ENOENT;
    int v = 0;
    for (; path[i]; i++)
    {
        if (path[i] < '0' || path[i] > '9')
            return -ENOENT;
        v = v * 10 + (path[i] - '0');
        if (v >= PTYS)
            return -ENOENT;
    }
    if (!ptys[v].used)
        return -ENOENT;
    *idx = v;
    return 0;
}
int pty_stat(const char *path, uint32_t *mode, uint64_t *ino, uint64_t *rdev, uint32_t *uid)
{
    int idx;
    int r = pts_parse(path, &idx);
    if (r)
        return r;
    *uid = 0;
    if (idx < 0)
    {
        *mode = 0040000 | 0755;
        *ino = 1;
        *rdev = 0;
        return 0;
    }
    *mode = 0020000 | 0620;
    *ino = 2 + (uint64_t)idx;
    *rdev = (136ULL << 8) | (uint64_t)idx;
    *uid = ptys[idx].owner;
    return 0;
}
int64_t pty_open_slave_path(process_t *p, const char *path, int flags)
{
    int idx;
    int r = pts_parse(path, &idx);
    if (r)
        return r;
    if (idx < 0)
        return -EISDIR;
    struct tty *t = &ptys[idx];
    if (t->locked)
        return -EIO;
    struct kfile *f = kfile_alloc(FT_CONSOLE, flags & (KO_ACCMODE | O_NONBLOCK), 0);
    if (!f)
        return -ENFILE;
    f->obj = t;
    f->pkind = (136 << 8) | idx;
    t->slave_refs++;
    t->slave_seen = 1;
    if (!(flags & 0400) && p->sid == p->pid && !t->session)
    {
        int has = 0;
        for (int i = 0; i < PTYS; i++)
            if (ptys[i].used && ptys[i].session == p->sid)
                has = 1;
        if (!has)
        {
            t->session = p->sid;
            t->fg_pgrp = p->pgid;
        }
    }
    int fd = fd_install(p, f, 0, (flags & O_CLOEXEC) != 0);
    if (fd < 0)
    {
        kfile_put(f);
        return fd;
    }
    return fd;
}
void pty_attach_ctty(process_t *p, struct kfile *f)
{
    for (int i = 0; i < PTYS; i++)
    {
        struct tty *t = &ptys[i];
        if (t->used && t->session && t->session == p->sid)
        {
            f->obj = t;
            f->pkind = (5 << 8);
            t->slave_refs++;
            t->slave_seen = 1;
            return;
        }
    }
}
void pty_slave_close(struct kfile *f)
{
    struct tty *t = ttyof(f);
    if (!t->pty)
        return;
    if (t->slave_refs > 0)
        t->slave_refs--;
    wake_master(t);
    pty_release_check(t);
}
int64_t pty_master_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    struct tty *t = f->obj;
    if (t->mh == t->mt)
    {
        if (t->slave_seen && t->slave_refs == 0)
            return -EIO;
        if (f->flags & O_NONBLOCK)
            return -EAGAIN;
        return E_BLOCK;
    }
    uint8_t tmp[128];
    int64_t total = 0;
    while ((uint64_t)total < n && t->mh != t->mt)
    {
        int k = 0;
        while (k < (int)sizeof tmp && (uint64_t)(total + k) < n && t->mh != t->mt)
        {
            tmp[k++] = t->mring[t->mt];
            t->mt = (t->mt + 1) % MRING;
        }
        if (copy_to_user(p, ubuf + total, tmp, (uint64_t)k) < 0)
            return total ? total : -EFAULT;
        total += k;
    }
    wakeup(&t->wchan);
    wakeup(&poll_chan);
    return total;
}
int64_t pty_master_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    struct tty *t = f->obj;
    uint8_t tmp[128];
    int64_t total = 0;
    while ((uint64_t)total < n)
    {
        uint64_t c = n - total;
        if (c > sizeof tmp)
            c = sizeof tmp;
        if (copy_from_user(p, tmp, ubuf + total, c) < 0)
            return total ? total : -EFAULT;
        for (uint64_t i = 0; i < c; i++)
            ld_input(t, tmp[i]);
        total += c;
    }
    return total;
}
int pty_master_poll(struct kfile *f)
{
    struct tty *t = f->obj;
    int m = 4;
    if (t->mh != t->mt)
        m |= 1;
    if (t->slave_seen && t->slave_refs == 0)
        m |= 0x10 | 1;
    return m;
}
int pty_slave_poll(struct kfile *f)
{
    struct tty *t = ttyof(f);
    int m = 0;
    if (!t->pty)
        return (tty_poll(f) ? 1 : 0) | 4;
    if (tty_poll(f))
        m |= 1;
    if (mring_free(t) >= 2)
        m |= 4;
    if (!t->master_open)
        m |= 0x10 | 1;
    return m;
}
void pty_master_close(struct kfile *f)
{
    struct tty *t = f->obj;
    if (!t)
        return;
    t->master_open = 0;
    signal_fg(t, 1);
    wakeup(&t->chan);
    wakeup(&t->wchan);
    wakeup(&poll_chan);
    pty_release_check(t);
    f->obj = 0;
}
int64_t pty_master_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg)
{
    struct tty *t = f->obj;
    switch (req)
    {
    case TIOCGPTN:
    {
        uint32_t v = (uint32_t)pty_index(t);
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case TIOCSPTLCK:
    {
        int v;
        if (copy_from_user(p, &v, arg, sizeof v) < 0)
            return -EFAULT;
        t->locked = v != 0;
        return 0;
    }
    case TIOCGPTLCK:
    {
        int v = t->locked;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case FIONREAD_:
    {
        int v = (int)((t->mh + MRING - t->mt) % MRING);
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case TIOCSCTTY: case TIOCSPGRP: case TIOCGPGRP:
        return -ENOTTY;
    }
    return tty_common_ioctl(p, t, req, arg);
}
