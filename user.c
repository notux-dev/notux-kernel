
typedef unsigned long uint64_t;
typedef long int64_t;

static inline long syscall3(long num, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "memory", "cc"
    );
    return ret;
}

#define O_WRONLY 0x0001
#define O_CREAT  0x0040
#define O_TRUNC  0x0200
#define O_APPEND 0x0400

static long sys_open(const char *path, int flags)
{
    return syscall3(2, (long)path, flags, 0);
}
static long sys_close(int fd)
{
    return syscall3(3, fd, 0, 0);
}
static long sys_write(int fd, const char *buf, unsigned long len)
{
    return syscall3(1, fd, (long)buf, (long)len);
}
static long sys_dup2(int oldfd, int newfd)
{
    return syscall3(33, oldfd, newfd, 0);
}
static long sys_execve(const char *path)
{
    return syscall3(59, (long)path, 0, 0);
}
static long sys_wait4(int *status)
{
    return syscall3(61, 0, (long)status, 0);
}
static void sys_exit(int code)
{
    syscall3(60, code, 0, 0);
    for (;;) { }
}

static unsigned long strlen_(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

/* tiny signed-decimal formatter, no libc available */
static int itoa_(long v, char *out)
{
    char tmp[24];
    int n = 0;
    int neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (u == 0) tmp[n++] = '0';
    while (u) {
        tmp[n++] = '0' + (u % 10);
        u /= 10;
    }
    int len = 0;
    if (neg) out[len++] = '-';
    while (n > 0) out[len++] = tmp[--n];
    out[len] = '\0';
    return len;
}

void _start(void)
{
    const char *logpath = "/etc/result.log";

    /* 1) create/truncate the log file and write a startup line through
     * it directly (fd 3-ish from open(), not yet fd 1). */
    long fd = sys_open(logpath, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {

        sys_exit(1);
    }
    {
        const char *m = "init: start\n";
        sys_write((int)fd, m, strlen_(m));
    }

    sys_dup2((int)fd, 1);
    sys_close((int)fd);
    long child_pid = sys_execve("/bin/child");
    if (child_pid < 0) {
        long fd2 = sys_open(logpath, O_WRONLY | O_APPEND);
        if (fd2 >= 0) {
            const char *m = "init: execve FAILED\n";
            sys_write((int)fd2, m, strlen_(m));
            sys_close((int)fd2);
        }
        sys_exit(1);
    }
    int status = -1;
    long reaped = -1;
    for (long tries = 0; tries < 10000000 && reaped < 0; tries++) {
        reaped = sys_wait4(&status);
    }
    long fd3 = sys_open(logpath, O_WRONLY | O_APPEND);
    if (fd3 >= 0) {
        if (reaped == child_pid) {
            char buf[64];
            const char *a = "init: reaped child, status=";
            sys_write((int)fd3, a, strlen_(a));
            char numbuf[24];
            int len = itoa_(status, numbuf);
            sys_write((int)fd3, numbuf, (unsigned long)len);
            const char *b = "\n";
            sys_write((int)fd3, b, 1);
            (void)buf;
        } else {
            const char *m = "init: wait4 did not reap the child in time\n";
            sys_write((int)fd3, m, strlen_(m));
        }
        sys_close((int)fd3);
    }

    sys_exit(0);
}
