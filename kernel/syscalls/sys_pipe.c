#include "syscalls.h"
#include "process.h"
#include "uaccess.h"
#include "kfile.h"
#include "pmm.h"


#define PIPE_SIZE 4096
#define NPIPE 32

struct kpipe {
    int used;
    int readers, writers;
    uint32_t rpos, count;
    uint8_t *buf;
};

static struct kpipe pipes[NPIPE];

struct kpipe *pipe_create(void)
{
    for (int i = 0; i < NPIPE; i++)
        if (!pipes[i].used)
        {
            void *pg = pmm_alloc_page();
            if (!pg)
                return 0;
            pipes[i].used = 1;
            pipes[i].readers = pipes[i].writers = 1;
            pipes[i].rpos = pipes[i].count = 0;
            pipes[i].buf = pg;
            return &pipes[i];
        }
    return 0;
}

void pipe_close_end(struct kfile *f)
{
    struct kpipe *pp = f->pipe;
    if (!pp)
        return;
    if (f->type == FT_PIPE_R)
        pp->readers = 0;
    else
        pp->writers = 0;
    wakeup(pp);
    wakeup(&poll_chan);
    if (!pp->readers && !pp->writers)
    {
        pmm_free_page(pp->buf);
        pp->buf = 0;
        pp->used = 0;
    }
}

int64_t pipe_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    struct kpipe *pp = f->pipe;
    if (pp->count == 0)
    {
        if (!pp->writers)
            return 0;
        if (f->flags & O_NONBLOCK)
            return -EAGAIN;
        return E_BLOCK;
    }
    uint64_t take = n < pp->count ? n : pp->count;
    uint64_t done = 0;
    while (done < take)
    {
        uint64_t chunk = take - done;
        uint64_t till_wrap = PIPE_SIZE - pp->rpos;
        if (chunk > till_wrap)
            chunk = till_wrap;
        if (copy_to_user(p, ubuf + done, pp->buf + pp->rpos, chunk) < 0)
        {
            if (done)
                break;
            return -EFAULT;
        }
        pp->rpos = (pp->rpos + chunk) % PIPE_SIZE;
        pp->count -= chunk;
        done += chunk;
    }
    if (done)
        { wakeup(pp); wakeup(&poll_chan); }
    return (int64_t)done;
}

int64_t pipe_write(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    struct kpipe *pp = f->pipe;
    if (!pp->readers)
    {
        signal_post(p, 13);
        return -EPIPE;
    }
    uint64_t room = PIPE_SIZE - pp->count;
    uint64_t need = n <= PIPE_SIZE ? n : 1;
    if (room < need)
    {
        if (f->flags & O_NONBLOCK)
            return -EAGAIN;
        return E_BLOCK;
    }
    uint64_t put = n < room ? n : room;
    uint64_t done = 0;
    while (done < put)
    {
        uint64_t wpos = (pp->rpos + pp->count) % PIPE_SIZE;
        uint64_t chunk = put - done;
        if (chunk > PIPE_SIZE - wpos)
            chunk = PIPE_SIZE - wpos;
        if (copy_from_user(p, pp->buf + wpos, ubuf + done, chunk) < 0)
        {
            if (done)
                break;
            return -EFAULT;
        }
        pp->count += chunk;
        done += chunk;
    }
    if (done)
        { wakeup(pp); wakeup(&poll_chan); }
    return (int64_t)done;
}

int pipe_nread(struct kfile *f)
{
    return f->pipe ? (int)f->pipe->count : 0;
}

int pipe_poll(struct kfile *f)
{
    struct kpipe *pp = f->pipe;
    if (f->type == FT_PIPE_R)
    {
        int r = 0;
        if (pp->count)
            r |= 1;
        if (!pp->writers)
            r |= 0x10;
        return r;
    }
    int r = 0;
    if (!pp->readers)
        return 8;
    if (pp->count < PIPE_SIZE)
        r |= 4;
    return r;
}

static int64_t do_pipe(uint64_t uptr, int flags)
{
    process_t *p = get_current_process();
    if (flags & ~(O_CLOEXEC | O_NONBLOCK))
        return -EINVAL;
    struct kpipe *pp = pipe_create();
    if (!pp)
        return -ENFILE;
    int nb = flags & O_NONBLOCK;
    struct kfile *r = kfile_alloc(FT_PIPE_R, 0 | nb, 0);
    struct kfile *w = kfile_alloc(FT_PIPE_W, 1 | nb, 0);
    if (!r || !w)
    {
        if (r) { r->pipe = pp; kfile_put(r); } else pp->readers = 0;
        if (w) { w->pipe = pp; kfile_put(w); } else pp->writers = 0;
        if (!r && !w) { pmm_free_page(pp->buf); pp->buf = 0; pp->used = 0; }
        return -ENFILE;
    }
    r->pipe = pp;
    w->pipe = pp;
    int cx = (flags & O_CLOEXEC) != 0;
    int rfd = fd_install(p, r, 0, cx);
    if (rfd < 0)
    {
        kfile_put(r); kfile_put(w);
        return rfd;
    }
    int wfd = fd_install(p, w, 0, cx);
    if (wfd < 0)
    {
        p->fds[rfd] = 0;
        kfile_put(r); kfile_put(w);
        return wfd;
    }
    int fds[2] = { rfd, wfd };
    if (copy_to_user(p, uptr, fds, sizeof fds) < 0)
    {
        p->fds[rfd] = 0; p->fds[wfd] = 0;
        kfile_put(r); kfile_put(w);
        return -EFAULT;
    }
    return 0;
}

uint64_t sys_pipe(uint64_t *regs)
{
    regs[14] = (uint64_t)do_pipe(regs[9], 0);
    return (uint64_t)regs;
}

uint64_t sys_pipe2(uint64_t *regs)
{
    regs[14] = (uint64_t)do_pipe(regs[9], (int)regs[10]);
    return (uint64_t)regs;
}
