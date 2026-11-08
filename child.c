
typedef unsigned long uint64_t;

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

static long sys_write(int fd, const char *buf, unsigned long len)
{
    return syscall3(1, fd, (long)buf, (long)len);
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

void _start(void)
{
    const char *msg =
        "child: hello via inherited stdout\n";
    sys_write(1, msg, strlen_(msg));

    const char *bye =
        "child: exiting 42\n";
    sys_write(1, bye, strlen_(bye));

    sys_exit(42);
}
