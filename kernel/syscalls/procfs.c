#include "syscalls.h"
#include "procfs.h"
#include "ext2.h"
#include "uaccess.h"
#include "pmm.h"
#include "ktime.h"


enum {
    PN_NONE = 0, PN_ROOT, PN_CPUINFO, PN_MEMINFO, PN_UPTIME, PN_VERSION,
    PN_MOUNTS, PN_LOADAVG, PN_STAT, PN_FILESYSTEMS,
    PN_PIDDIR, PN_PID_STATUS, PN_PID_STAT, PN_PID_CMDLINE
};

struct pnode { int kind; uint32_t pid; };

static const struct { const char *name; int kind; } top_files[] = {
    {"cpuinfo", PN_CPUINFO}, {"meminfo", PN_MEMINFO}, {"uptime", PN_UPTIME},
    {"version", PN_VERSION}, {"mounts", PN_MOUNTS}, {"loadavg", PN_LOADAVG},
    {"stat", PN_STAT}, {"filesystems", PN_FILESYSTEMS},
};
#define NTOP ((int)(sizeof top_files / sizeof top_files[0]))

static const struct { const char *name; int kind; } pid_files[] = {
    {"status", PN_PID_STATUS}, {"stat", PN_PID_STAT}, {"cmdline", PN_PID_CMDLINE},
};
#define NPID ((int)(sizeof pid_files / sizeof pid_files[0]))

static int is_dir(int kind) { return kind == PN_ROOT || kind == PN_PIDDIR; }

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int64_t parse_pid(const char *s)
{
    if (!*s)
        return -1;
    int64_t v = 0;
    for (; *s; s++)
    {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (*s - '0');
        if (v > 0x7fffffff)
            return -1;
    }
    return v;
}

static int lookup(process_t *p, const char *path, struct pnode *n)
{
    if (path[0] != '/' || path[1] != 'p' || path[2] != 'r' || path[3] != 'o' ||
        path[4] != 'c' || (path[5] != 0 && path[5] != '/'))
        return PROC_NOTMINE;
    char comp[2][24];
    int nc = 0;
    const char *s = path + 5;
    while (*s)
    {
        while (*s == '/')
            s++;
        if (!*s)
            break;
        int l = 0;
        char tmp[24];
        while (*s && *s != '/')
        {
            if (l < 23)
                tmp[l] = *s;
            l++;
            s++;
        }
        if (l >= 23)
            return -ENOENT;
        tmp[l] = 0;
        if (streq(tmp, "."))
            continue;
        if (nc == 2)
            return -ENOENT;
        for (int i = 0; i <= l; i++)
            comp[nc][i] = tmp[i];
        nc++;
    }
    n->pid = 0;
    if (nc == 0)
    {
        n->kind = PN_ROOT;
        return 0;
    }
    int64_t pid;
    if (streq(comp[0], "self"))
        pid = p->pid;
    else
        pid = parse_pid(comp[0]);
    if (pid > 0)
    {
        process_t *t = get_process_by_pid((uint32_t)pid);
        if (!t)
            return -ENOENT;
        n->pid = (uint32_t)pid;
        if (nc == 1)
        {
            n->kind = PN_PIDDIR;
            return 0;
        }
        for (int i = 0; i < NPID; i++)
            if (streq(comp[1], pid_files[i].name))
            {
                n->kind = pid_files[i].kind;
                return 0;
            }
        return -ENOENT;
    }
    if (nc != 1)
        return -ENOENT;
    for (int i = 0; i < NTOP; i++)
        if (streq(comp[0], top_files[i].name))
        {
            n->kind = top_files[i].kind;
            return 0;
        }
    return -ENOENT;
}

static uint64_t node_ino(const struct pnode *n)
{
    return 0x70000000ULL + ((uint64_t)n->pid << 8) + (uint64_t)n->kind;
}

int proc_stat(process_t *p, const char *path, uint32_t *mode, uint64_t *ino)
{
    struct pnode n;
    int r = lookup(p, path, &n);
    if (r)
        return r;
    *mode = is_dir(n.kind) ? (0040000 | 0555) : (0100000 | 0444);
    *ino = node_ino(&n);
    return 0;
}


struct sb { char *b; int n, cap; };

static void sb_c(struct sb *s, char c)
{
    if (s->n < s->cap - 1)
        s->b[s->n++] = c;
}
static void sb_s(struct sb *s, const char *t)
{
    while (*t)
        sb_c(s, *t++);
}
static void sb_u(struct sb *s, uint64_t v)
{
    char t[24];
    int k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k)
        sb_c(s, t[--k]);
}
static void sb_i(struct sb *s, int64_t v)
{
    if (v < 0)
    {
        sb_c(s, '-');
        v = -v;
    }
    sb_u(s, (uint64_t)v);
}
static void sb_x16(struct sb *s, uint64_t v)
{
    for (int i = 15; i >= 0; i--)
        sb_c(s, "0123456789abcdef"[(v >> (i * 4)) & 15]);
}
static void sb_2(struct sb *s, uint64_t v)
{
    sb_c(s, (char)('0' + (v / 10) % 10));
    sb_c(s, (char)('0' + v % 10));
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static void gen_cpuinfo(struct sb *s)
{
    uint32_t a, b, c, d;
    char vendor[13];
    cpuid(0, &a, &b, &c, &d);
    *(uint32_t *)(vendor + 0) = b;
    *(uint32_t *)(vendor + 4) = d;
    *(uint32_t *)(vendor + 8) = c;
    vendor[12] = 0;
    uint32_t maxl = a;
    uint32_t eax1 = 0, ecx1 = 0, edx1 = 0;
    if (maxl >= 1)
    {
        cpuid(1, &eax1, &b, &ecx1, &edx1);
    }
    uint32_t fam = (eax1 >> 8) & 0xf, model = (eax1 >> 4) & 0xf, step = eax1 & 0xf;
    if (fam == 6 || fam == 15)
        model |= ((eax1 >> 16) & 0xf) << 4;
    if (fam == 15)
        fam += (eax1 >> 20) & 0xff;
    char brand[49];
    brand[0] = 0;
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a >= 0x80000004u)
    {
        for (uint32_t i = 0; i < 3; i++)
        {
            cpuid(0x80000002u + i, &a, &b, &c, &d);
            *(uint32_t *)(brand + i * 16 + 0) = a;
            *(uint32_t *)(brand + i * 16 + 4) = b;
            *(uint32_t *)(brand + i * 16 + 8) = c;
            *(uint32_t *)(brand + i * 16 + 12) = d;
        }
        brand[48] = 0;
    }
    const char *bn = brand;
    while (*bn == ' ')
        bn++;
    if (!*bn)
        bn = "unknown";
    sb_s(s, "processor\t: 0\nvendor_id\t: ");
    sb_s(s, vendor);
    sb_s(s, "\ncpu family\t: "); sb_u(s, fam);
    sb_s(s, "\nmodel\t\t: "); sb_u(s, model);
    sb_s(s, "\nmodel name\t: "); sb_s(s, bn);
    sb_s(s, "\nstepping\t: "); sb_u(s, step);
    sb_s(s, "\nphysical id\t: 0\nsiblings\t: 1\ncore id\t\t: 0\ncpu cores\t: 1\n");
    sb_s(s, "fpu\t\t: yes\nflags\t\t:");
    static const struct { int bit; const char *n; } fe[] = {
        {0, "fpu"}, {4, "tsc"}, {5, "msr"}, {6, "pae"}, {8, "cx8"}, {9, "apic"},
        {11, "sep"}, {13, "pge"}, {15, "cmov"}, {19, "clflush"}, {23, "mmx"},
        {24, "fxsr"}, {25, "sse"}, {26, "sse2"},
    };
    for (unsigned i = 0; i < sizeof fe / sizeof fe[0]; i++)
        if (edx1 & (1u << fe[i].bit))
        {
            sb_c(s, ' ');
            sb_s(s, fe[i].n);
        }
    static const struct { int bit; const char *n; } fc[] = {
        {0, "pni"}, {9, "ssse3"}, {19, "sse4_1"}, {20, "sse4_2"}, {23, "popcnt"},
    };
    for (unsigned i = 0; i < sizeof fc / sizeof fc[0]; i++)
        if (ecx1 & (1u << fc[i].bit))
        {
            sb_c(s, ' ');
            sb_s(s, fc[i].n);
        }
    sb_s(s, " lm\n\n");
}

static void gen_meminfo(struct sb *s)
{
    uint64_t total = (uint64_t)pmm_total_pages() * 4, fr = (uint64_t)pmm_free_pages() * 4;
    struct { const char *k; uint64_t v; } l[] = {
        {"MemTotal", total}, {"MemFree", fr}, {"MemAvailable", fr},
        {"Buffers", 0}, {"Cached", 0}, {"SwapCached", 0},
        {"SwapTotal", 0}, {"SwapFree", 0},
    };
    for (unsigned i = 0; i < sizeof l / sizeof l[0]; i++)
    {
        sb_s(s, l[i].k);
        sb_s(s, ":");
        int pad = 15 - (int)0;
        (void)pad;
        sb_c(s, ' ');
        char t[24];
        int k = 0;
        uint64_t v = l[i].v;
        do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        for (int j = k; j < 11; j++)
            sb_c(s, ' ');
        while (k)
            sb_c(s, t[--k]);
        sb_s(s, " kB\n");
    }
}

static int count_procs(void)
{
    int c = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i].state != PROCESS_STATE_TERMINATED)
            c++;
    return c;
}

static const char *comm_of(process_t *t)
{
    return t->comm[0] ? t->comm : "notux";
}

static char state_char(process_t *t)
{
    switch (t->state)
    {
    case PROCESS_STATE_WAITING: return 'S';
    case PROCESS_STATE_ZOMBIE:  return 'Z';
    default:                    return 'R';
    }
}

static void gen_pid_status(struct sb *s, process_t *t)
{
    sb_s(s, "Name:\t"); sb_s(s, comm_of(t));
    sb_s(s, "\nState:\t"); sb_c(s, state_char(t));
    sb_s(s, t->state == PROCESS_STATE_ZOMBIE ? " (zombie)" :
            t->state == PROCESS_STATE_WAITING ? " (sleeping)" : " (running)");
    sb_s(s, "\nTgid:\t"); sb_u(s, t->pid);
    sb_s(s, "\nPid:\t"); sb_u(s, t->pid);
    sb_s(s, "\nPPid:\t"); sb_u(s, t->ppid);
    sb_s(s, "\nUid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\nThreads:\t1\n");
    sb_s(s, "SigPnd:\t"); sb_x16(s, t->sigpending);
    sb_s(s, "\nSigBlk:\t"); sb_x16(s, t->sigmask);
    sb_c(s, '\n');
}

static void gen_pid_stat(struct sb *s, process_t *t)
{
    int64_t f[53];
    for (int i = 0; i < 53; i++)
        f[i] = 0;
    f[4] = t->ppid; f[5] = t->pid; f[6] = t->pid; f[8] = -1; f[20] = 1;
    sb_u(s, t->pid);
    sb_s(s, " ("); sb_s(s, comm_of(t)); sb_s(s, ") ");
    sb_c(s, state_char(t));
    for (int i = 4; i <= 52; i++)
    {
        sb_c(s, ' ');
        sb_i(s, f[i]);
    }
    sb_c(s, '\n');
}

static int gen(process_t *cur, struct pnode *n, char *buf, int cap)
{
    struct sb s = { buf, 0, cap };
    uint64_t t = ktime_ticks;
    switch (n->kind)
    {
    case PN_CPUINFO: gen_cpuinfo(&s); break;
    case PN_MEMINFO: gen_meminfo(&s); break;
    case PN_UPTIME:
        sb_u(&s, t / 100); sb_c(&s, '.'); sb_2(&s, t % 100);
        sb_c(&s, ' '); sb_u(&s, t / 100); sb_c(&s, '.'); sb_2(&s, t % 100);
        sb_c(&s, '\n');
        break;
    case PN_VERSION:
        sb_s(&s, "notux version 6.1.0 (build@notux) #1 SMP\n");
        break;
    case PN_MOUNTS:
        sb_s(&s, ext2_is_root() ? "/dev/sda / ext2 rw 0 0\nproc /proc proc rw 0 0\n" : "/dev/sda / xv6fs rw 0 0\nproc /proc proc rw 0 0\n");
        break;
    case PN_LOADAVG:
        sb_s(&s, "0.00 0.00 0.00 1/"); sb_u(&s, count_procs());
        sb_c(&s, ' '); sb_u(&s, cur->pid); sb_c(&s, '\n');
        break;
    case PN_STAT:
    {
        uint64_t bt = ktime_realtime_ns() / 1000000000ULL;
        bt = bt > t / 100 ? bt - t / 100 : 0;
        sb_s(&s, "cpu  0 0 0 "); sb_u(&s, t); sb_s(&s, " 0 0 0 0 0 0\n");
        sb_s(&s, "cpu0 0 0 0 "); sb_u(&s, t); sb_s(&s, " 0 0 0 0 0 0\n");
        sb_s(&s, "intr 0\nctxt 0\nbtime "); sb_u(&s, bt);
        sb_s(&s, "\nprocesses "); sb_u(&s, count_procs());
        sb_s(&s, "\nprocs_running 1\nprocs_blocked 0\n");
        break;
    }
    case PN_FILESYSTEMS:
        sb_s(&s, "nodev\tproc\n\txv6fs\n");
        break;
    case PN_PID_STATUS:
    case PN_PID_STAT:
    {
        process_t *tp = get_process_by_pid(n->pid);
        if (!tp)
            return -ENOENT;
        if (n->kind == PN_PID_STATUS)
            gen_pid_status(&s, tp);
        else
            gen_pid_stat(&s, tp);
        break;
    }
    case PN_PID_CMDLINE:
        break;
    default:
        return -EISDIR;
    }
    return s.n;
}


int64_t proc_open(process_t *p, const char *path, int flags)
{
    struct pnode n;
    int r = lookup(p, path, &n);
    if (r == PROC_NOTMINE)
        return PROC_NOTMINE;
    if (r < 0)
        return (flags & KO_CREAT) ? -EACCES : r;
    if ((flags & KO_CREAT) && (flags & O_EXCL))
        return -EEXIST;
    if ((flags & KO_ACCMODE) != 0 || (flags & KO_TRUNC))
        return is_dir(n.kind) ? -EISDIR : -EACCES;
    if ((flags & O_DIRECTORY) && !is_dir(n.kind))
        return -ENOTDIR;

    struct kfile *f = kfile_alloc(FT_PROC, flags & (KO_ACCMODE | O_NONBLOCK), 0);
    if (!f)
        return -ENFILE;
    f->pkind = n.kind;
    f->ppid = n.pid;
    if (!is_dir(n.kind))
    {
        f->pbuf = pmm_alloc_page();
        if (!f->pbuf)
        {
            kfile_put(f);
            return -ENOMEM;
        }
        int len = gen(p, &n, f->pbuf, 4096);
        if (len < 0)
        {
            kfile_put(f);
            return len;
        }
        f->plen = (uint32_t)len;
    }
    int fd = fd_install(p, f, 0, (flags & O_CLOEXEC) != 0);
    if (fd < 0)
    {
        kfile_put(f);
        return fd;
    }
    return fd;
}

int64_t proc_read(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t n)
{
    if (is_dir(f->pkind))
        return -EISDIR;
    if (f->off >= f->plen)
        return 0;
    uint64_t avail = f->plen - f->off;
    if (n > avail)
        n = avail;
    if (copy_to_user(p, ubuf, f->pbuf + f->off, n) < 0)
        return -EFAULT;
    f->off += n;
    return (int64_t)n;
}

void proc_release(struct kfile *f)
{
    if (f->pbuf)
        pmm_free_page(f->pbuf);
    f->pbuf = 0;
    f->plen = 0;
}

int proc_f_is_dir(struct kfile *f) { return is_dir(f->pkind); }


static int dir_entry(int kind, uint32_t pid, uint64_t idx, char *name, int *dt, uint64_t *ino)
{
    (void)pid;
    const char *nm = 0;
    *dt = 8;
    *ino = 0x70000000ULL + idx;
    if (idx == 0) { nm = "."; *dt = 4; }
    else if (idx == 1) { nm = ".."; *dt = 4; }
    else if (kind == PN_PIDDIR)
    {
        uint64_t k = idx - 2;
        if (k >= (uint64_t)NPID)
            return 0;
        nm = pid_files[k].name;
    }
    else
    {
        uint64_t k = idx - 2;
        if (k < (uint64_t)NTOP)
            nm = top_files[k].name;
        else if (k == (uint64_t)NTOP)
        {
            nm = "self";
            *dt = 4;
        }
        else
        {
            uint64_t want = k - NTOP - 1;
            for (int i = 0; i < MAX_PROCESSES; i++)
                if (processes[i].state != PROCESS_STATE_TERMINATED && processes[i].pid)
                {
                    if (want == 0)
                    {
                        char t[12];
                        int l = 0;
                        uint32_t v = processes[i].pid;
                        do { t[l++] = (char)('0' + v % 10); v /= 10; } while (v);
                        for (int j = 0; j < l; j++)
                            name[j] = t[l - 1 - j];
                        name[l] = 0;
                        *dt = 4;
                        *ino = 0x70000000ULL + ((uint64_t)processes[i].pid << 8);
                        return 1;
                    }
                    want--;
                }
            return 0;
        }
    }
    int i = 0;
    while (nm[i] && i < 23)
    {
        name[i] = nm[i];
        i++;
    }
    name[i] = 0;
    return 1;
}

int64_t proc_getdents(process_t *p, struct kfile *f, uint64_t ubuf, uint64_t count)
{
    if (!is_dir(f->pkind))
        return -ENOTDIR;
    uint64_t written = 0;
    for (;;)
    {
        char name[24];
        int dt;
        uint64_t ino;
        if (!dir_entry(f->pkind, f->ppid, f->off, name, &dt, &ino))
            break;
        int n = 0;
        while (name[n])
            n++;
        uint16_t reclen = (uint16_t)((19 + n + 1 + 7) & ~7);
        if (written + reclen > count)
        {
            if (!written)
                return -EINVAL;
            break;
        }
        uint8_t rec[48];
        for (int i = 0; i < 48; i++)
            rec[i] = 0;
        *(uint64_t *)(rec + 0) = ino;
        *(uint64_t *)(rec + 8) = f->off + 1;
        *(uint16_t *)(rec + 16) = reclen;
        rec[18] = (uint8_t)dt;
        for (int i = 0; i < n; i++)
            rec[19 + i] = (uint8_t)name[i];
        if (copy_to_user(p, ubuf + written, rec, reclen) < 0)
            return written ? (int64_t)written : -EFAULT;
        written += reclen;
        f->off++;
    }
    return (int64_t)written;
}
