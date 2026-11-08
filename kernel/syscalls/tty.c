#include "syscalls.h"
#include "tty.h"
#include "console.h"
#include "uaccess.h"
#include "ktime.h"


struct ktermios {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
};

#define IGNBRK 0x1
#define ISTRIP 0x20
#define INLCR  0x40
#define IGNCR  0x80
#define ICRNL  0x100
#define OPOST  0x1
#define ONLCR  0x4
#define OCRNL  0x8
#define ISIG   0x1
#define ICANON 0x2
#define ECHO   0x8
#define ECHOE  0x10
#define ECHOK  0x20
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

static struct ktermios tio;
static struct { uint16_t row, col, xpix, ypix; } ws;
static uint32_t fg_pgrp = 1;
static uint32_t tty_session = 1;

#define RQ 1024
static uint8_t rq[RQ];
static uint8_t rqm[RQ];
static uint32_t rhead, rtail, nlines;
static int eof_pending;
#define LINE 256
static uint8_t line[LINE];
static int linelen;
static int lnext;
char tty_chan;

void tty_init(void)
{
    tio.c_iflag = ICRNL | 0x400;
    tio.c_oflag = OPOST | ONLCR;
    tio.c_cflag = 0x4BF;
    tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
    tio.c_cc[VINTR] = 3; tio.c_cc[VQUIT] = 28; tio.c_cc[VERASE] = 127;
    tio.c_cc[VKILL] = 21; tio.c_cc[VEOF] = 4; tio.c_cc[VTIME] = 0; tio.c_cc[VMIN] = 1;
    tio.c_cc[VSTART] = 17; tio.c_cc[VSTOP] = 19; tio.c_cc[VSUSP] = 26;
    tio.c_cc[VREPRINT] = 18; tio.c_cc[VDISCARD] = 15; tio.c_cc[VWERASE] = 23;
    tio.c_cc[VLNEXT] = 22;
    ws.row = (uint16_t)console_rows();
    ws.col = (uint16_t)console_cols();
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

static void out_raw(uint8_t c)
{
    serputc((char)c);
    screen_byte(c);
}

static void out_byte(uint8_t c)
{
    if (tio.c_oflag & OPOST)
    {
        if (c == '\n' && (tio.c_oflag & ONLCR))
            out_raw('\r');
        else if (c == '\r' && (tio.c_oflag & OCRNL))
            c = '\n';
    }
    out_raw(c);
}

void tty_write_kernel(const char *s, uint64_t n)
{
    console_cursor(0);
    for (uint64_t i = 0; i < n; i++)
        out_byte((uint8_t)s[i]);
    console_cursor(1);
}

int64_t tty_write(process_t *p, uint64_t ubuf, uint64_t n)
{
    uint8_t buf[256];
    int64_t total = 0;
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
            out_byte(buf[i]);
        total += c;
    }
    console_cursor(1);
    return total;
}


static void echo_byte(uint8_t c)
{
    if (!(tio.c_lflag & ECHO))
        return;
    if (c < 0x20 && c != '\n' && c != '\t' && (tio.c_lflag & ECHOCTL))
    {
        out_byte('^');
        out_byte((uint8_t)(c + '@'));
    }
    else if (c == 0x7f && (tio.c_lflag & ECHOCTL))
    {
        out_byte('^');
        out_byte('?');
    }
    else
        out_byte(c);
}

static int echo_width(uint8_t c)
{
    return ((c < 0x20 && c != '\t' && c != '\n') || c == 0x7f) && (tio.c_lflag & ECHOCTL) ? 2 : 1;
}

static void erase_one(void)
{
    if (linelen == 0)
        return;
    uint8_t c = line[--linelen];
    if ((tio.c_lflag & ECHO) && (tio.c_lflag & ECHOE))
        for (int i = echo_width(c); i > 0; i--)
        {
            out_byte('\b'); out_byte(' '); out_byte('\b');
        }
}

static void rq_push(uint8_t c, int mark)
{
    uint32_t next = (rhead + 1) % RQ;
    if (next == rtail)
        return;
    rq[rhead] = c;
    rqm[rhead] = (uint8_t)mark;
    rhead = next;
    if (mark)
        nlines++;
}

static void commit_line(int with_newline)
{
    for (int i = 0; i < linelen; i++)
        rq_push(line[i], 0);
    if (with_newline)
        rq_push('\n', 1);
    else if (linelen)
    {
        rqm[(rhead + RQ - 1) % RQ] = 1;
        nlines++;
    }
    else
        eof_pending = 1;
    linelen = 0;
}

static void signal_fg(int sig)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_t *t = &processes[i];
        if (t->state != PROCESS_STATE_TERMINATED && t->state != PROCESS_STATE_ZOMBIE &&
            t->pid && t->pgid == fg_pgrp)
            signal_post(t, sig);
    }
}

static void flush_input(void)
{
    rhead = rtail = 0;
    nlines = 0;
    linelen = 0;
    eof_pending = 0;
}

void tty_input(uint8_t c)
{
    console_cursor(0);
    if (tio.c_iflag & ISTRIP)
        c &= 0x7f;
    if (c == '\r')
    {
        if (tio.c_iflag & IGNCR)
            goto out;
        if (tio.c_iflag & ICRNL)
            c = '\n';
    }
    else if (c == '\n' && (tio.c_iflag & INLCR))
        c = '\r';

    if (lnext)
    {
        lnext = 0;
        goto store;
    }
    if (tio.c_lflag & ISIG)
    {
        int sig = 0;
        if (c == tio.c_cc[VINTR]) sig = 2;
        else if (c == tio.c_cc[VQUIT]) sig = 3;
        else if (c == tio.c_cc[VSUSP]) sig = 20;
        if (sig)
        {
            if (!(tio.c_lflag & NOFLSH))
                flush_input();
            echo_byte(c);
            if (tio.c_lflag & ECHO)
                out_byte('\n');
            signal_fg(sig);
            goto wake;
        }
    }
    if ((tio.c_lflag & IEXTEN) && c == tio.c_cc[VLNEXT] && (tio.c_lflag & ICANON))
    {
        lnext = 1;
        goto out;
    }
store:
    if (tio.c_lflag & ICANON)
    {
        if (c == tio.c_cc[VERASE] || c == 0x08)
        {
            erase_one();
            goto out;
        }
        if (c == tio.c_cc[VKILL])
        {
            while (linelen)
                erase_one();
            if ((tio.c_lflag & ECHO) && (tio.c_lflag & ECHOK) && !(tio.c_lflag & ECHOE))
                out_byte('\n');
            goto out;
        }
        if (c == tio.c_cc[VWERASE])
        {
            while (linelen && line[linelen - 1] == ' ')
                erase_one();
            while (linelen && line[linelen - 1] != ' ')
                erase_one();
            goto out;
        }
        if (c == tio.c_cc[VEOF])
        {
            commit_line(0);
            goto wake;
        }
        if (c == '\n')
        {
            if (tio.c_lflag & (ECHO | ECHONL))
                out_byte('\n');
            commit_line(1);
            goto wake;
        }
        if (linelen < LINE - 1)
        {
            line[linelen++] = c;
            echo_byte(c);
        }
        goto out;
    }
    rq_push(c, 0);
    if (tio.c_lflag & ECHO)
        echo_byte(c);
wake:
    wakeup(&tty_chan);
    wakeup(&poll_chan);
out:
    console_cursor(1);
}


static int canon(void) { return (tio.c_lflag & ICANON) != 0; }

static uint32_t rq_count(void)
{
    return (rhead + RQ - rtail) % RQ;
}

int tty_poll(void)
{
    if (canon())
        return (nlines || eof_pending) ? 1 : 0;
    return rq_count() ? 1 : 0;
}

int tty_nread(void)
{
    if (canon())
    {
        uint32_t n = 0;
        for (uint32_t i = rtail; i != rhead; i = (i + 1) % RQ)
        {
            n++;
            if (rqm[i])
                break;
        }
        return nlines ? (int)n : 0;
    }
    return (int)rq_count();
}

char kgetc(void)
{
    if (rtail == rhead)
        return 0;
    char c = (char)rq[rtail];
    if (rqm[rtail] && nlines)
        nlines--;
    rtail = (rtail + 1) % RQ;
    return c;
}
int kbd_has_char(void) { return rtail != rhead; }
void kflush(void) { flush_input(); }

int64_t tty_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n, void **chan)
{
    int nonblock = (f->flags & O_NONBLOCK) != 0;
    if (canon())
    {
        if (!nlines && !eof_pending)
        {
            if (nonblock)
                return -EAGAIN;
            *chan = &tty_chan;
            return E_BLOCK;
        }
        if (!nlines && eof_pending)
        {
            eof_pending = 0;
            return 0;
        }
    }
    else
    {
        uint32_t need = tio.c_cc[VMIN] ? tio.c_cc[VMIN] : 0;
        if (rq_count() == 0 || rq_count() < need)
        {
            if (need == 0)
                return nonblock ? -EAGAIN : 0;
            if (nonblock)
                return -EAGAIN;
            *chan = &tty_chan;
            return E_BLOCK;
        }
    }
    uint8_t tmp[128];
    int64_t total = 0;
    while ((uint64_t)total < n && rtail != rhead)
    {
        int k = 0;
        int ended = 0;
        while (k < (int)sizeof tmp && (uint64_t)(total + k) < n && rtail != rhead)
        {
            tmp[k++] = rq[rtail];
            if (rqm[rtail])
            {
                if (canon() && nlines)
                    nlines--;
                ended = 1;
                rtail = (rtail + 1) % RQ;
                break;
            }
            rtail = (rtail + 1) % RQ;
        }
        if (copy_to_user(p, ubuf + total, tmp, (uint64_t)k) < 0)
            return total ? total : -EFAULT;
        total += k;
        if (ended && canon())
            break;
        if (!canon() && ended && total >= 1 && rq_count() == 0)
            break;
    }
    return total;
}


int64_t tty_ioctl(process_t *p, struct kfile *f, uint64_t req, uint64_t arg)
{
    (void)f;
    switch (req)
    {
    case TCGETS:
        return copy_to_user(p, arg, &tio, sizeof tio) < 0 ? -EFAULT : 0;
    case TCSETS: case TCSETSW: case TCSETSF:
    {
        struct ktermios n;
        if (copy_from_user(p, &n, arg, sizeof n) < 0)
            return -EFAULT;
        if (req == TCSETSF)
            flush_input();
        int was_canon = canon();
        tio = n;
        nlines = 0;
        for (uint32_t i = rtail; i != rhead; i = (i + 1) % RQ)
            if (rqm[i])
                nlines++;
        if (was_canon && !canon())
        {
            for (int i = 0; i < linelen; i++)
                rq_push(line[i], 0);
            linelen = 0;
            wakeup(&tty_chan);
            wakeup(&poll_chan);
        }
        return 0;
    }
    case TCFLSH:
        if (arg == 0 || arg == 2)
            flush_input();
        return 0;
    case TCSBRK: case TCXONC: case TIOCEXCL: case TIOCNXCL:
        return 0;
    case TIOCGWINSZ:
        return copy_to_user(p, arg, &ws, sizeof ws) < 0 ? -EFAULT : 0;
    case TIOCSWINSZ:
        return copy_from_user(p, &ws, arg, sizeof ws) < 0 ? -EFAULT : 0;
    case TIOCGPGRP:
    {
        int v = (int)fg_pgrp;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case TIOCSPGRP:
    {
        int v;
        if (copy_from_user(p, &v, arg, sizeof v) < 0)
            return -EFAULT;
        if (v <= 0)
            return -EINVAL;
        fg_pgrp = (uint32_t)v;
        return 0;
    }
    case TIOCGSID:
    {
        int v = (int)tty_session;
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    case TIOCSCTTY:
        tty_session = p->sid;
        fg_pgrp = p->pgid;
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
        tty_input(c);
        return 0;
    }
    case FIONREAD_:
    {
        int v = tty_nread();
        return copy_to_user(p, arg, &v, sizeof v) < 0 ? -EFAULT : 0;
    }
    }
    return -ENOTTY;
}
