
typedef unsigned long uint64_t;
typedef long int64_t;

static inline long syscall3(long num, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory", "cc"
    );
    return ret;
}

static inline long syscall3_int80(long num, long a1, long a2, long a3)
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
static long sys_read(int fd, char *buf, unsigned long len)
{
    return syscall3(0, fd, (long)buf, (long)len);
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
static long sys_execve(const char *path, char *const *argv, char *const *envp)
{
    return syscall3(59, (long)path, (long)argv, (long)envp);
}
static long sys_arch_prctl(long code, long addr)
{
    return syscall3(158, code, addr, 0);
}
static long sys_wait4(long pid, int *status, long options)
{
    return syscall3(61, pid, (long)status, options);
}
static long sys_fork(void)
{
    return syscall3(57, 0, 0, 0);
}
#define WNOHANG 1
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20

static inline long syscall6(long num, long a1, long a2, long a3,
                            long a4, long a5, long a6)
{
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    return ret;
}
static long sys_mmap(long addr, long len, long prot, long flags, long fd, long off)
{
    return syscall6(9, addr, len, prot, flags, fd, off);
}
static long sys_munmap(long addr, long len) { return syscall3(11, addr, len, 0); }
static long sys_mprotect(long addr, long len, long prot) { return syscall3(10, addr, len, prot); }
static long sys_brk(long a) { return syscall3(12, a, 0, 0); }

#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
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

static void log_(const char *msg, long v)
{
    long fd = sys_open("/etc/result.log", O_WRONLY | O_APPEND);
    if (fd < 0) return;
    char num[24];
    int n = itoa_(v, num);
    sys_write((int)fd, msg, strlen_(msg));
    sys_write((int)fd, num, (unsigned long)n);
    sys_write((int)fd, "\n", 1);
    sys_close((int)fd);
}

static void shell_loop(void)
{
    static char *av[] = {"mksh", 0};
    static char *ev[] = {"HOME=/root", "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/mnt/bin", "TERM=linux", "PS1=notux# ", "USER=root", 0};
    for (;;) {
        long k = sys_fork();
        if (k == 0) {
            syscall3(112, 0, 0, 0);
            long t = sys_open("/dev/tty", 2);
            if (t < 0)
                sys_exit(126);
            syscall3(16, t, 0x540E, 0);
            sys_dup2((int)t, 0);
            sys_dup2((int)t, 1);
            sys_dup2((int)t, 2);
            if (t > 2)
                sys_close((int)t);
            sys_write(1, "notux: starting mksh (Ctrl+D to exit, it restarts)\n", 51);
            sys_execve("/bin/mksh", av, ev);
            sys_write(2, "notux: /bin/mksh not found\n", 28);
            sys_exit(127);
        }
        int st;
        for (;;) {
            long r = sys_wait4(-1, &st, 0);
            if (r == k || r < 0)
                break;
        }
    }
}

void _start(void)
{
    const char *logpath = "/etc/result.log";

    {
        long ifd = sys_open("/etc/interactive", 0);
        if (ifd >= 0) {
            sys_close((int)ifd);
            shell_loop();
        }
    }

    {
        long tfd = sys_open("/etc/inittab", 0);
        if (tfd >= 0) {
            static char *av[] = {"init", 0};
            static char *ev[] = {"HOME=/root", "PATH=/bin:/sbin:/usr/bin:/usr/sbin", "TERM=linux", "USER=root", 0};
            sys_close((int)tfd);
            sys_execve("/sbin/init", av, ev);
            sys_write(2, "notux: /sbin/init failed\n", 25);
        }
    }

    {
        static long act[4] = {1  , 0, 0, 0};
        syscall6(13, 2, (long)act, 0, 8, 0, 0);
        syscall6(13, 3, (long)act, 0, 8, 0, 0);
        syscall6(13, 20, (long)act, 0, 8, 0, 0);
    }

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

    long child_pid = sys_fork();
    if (child_pid == 0) {
        sys_execve("/bin/child", 0, 0);
        sys_exit(127);
    }
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
    long reaped = sys_wait4(child_pid, &status, 0);
    if (reaped == child_pid) {
        log_("init: reaped child, status=", WEXITSTATUS(status));
    } else {
        log_("init: wait4 FAILED, ret=", reaped);
    }

    log_("init: wait4 with no children ret=", sys_wait4(-1, &status, 0));

    static volatile long shared = 1;
    long fp = sys_fork();
    if (fp == 0) {
        shared = 99;
        log_("fork child: shared(copy)=", shared);
        sys_exit(7);
    }
    long r = sys_wait4(fp, &status, 0);
    log_("fork: parent reaped ok=", r == fp);
    log_("fork: child exit=", WEXITSTATUS(status));
    log_("fork: parent shared=", shared);

    long sp = sys_fork();
    if (sp == 0) {
        for (volatile long i = 0; i < 30000000; i++) { }
        sys_exit(3);
    }
    long nh = sys_wait4(sp, &status, WNOHANG);
    log_("wnohang while running ret=", nh);
    r = sys_wait4(sp, &status, 0);
    log_("wnohang then blocking ok=", r == sp);
    log_("wnohang child exit=", WEXITSTATUS(status));

    long op = sys_fork();
    if (op == 0) {
        if (sys_fork() == 0) {
            for (volatile long i = 0; i < 20000000; i++) { }
            sys_exit(5);
        }
        sys_exit(4);
    }
    int got = 0, sum = 0;
    for (int k = 0; k < 2; k++) {
        r = sys_wait4(-1, &status, 0);
        if (r > 0) { got++; sum += WEXITSTATUS(status); }
    }
    log_("orphan: reaped count=", got);
    log_("orphan: exit sum=", sum);

    log_("entry: getpid via syscall==int80 ok=",
         syscall3(39, 0, 0, 0) == syscall3_int80(39, 0, 0, 0));
    log_("entry: getppid=", syscall3(110, 0, 0, 0));

    {
        long A = sys_mmap(0, 3 * 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        log_("mmap anon ok=", A > 0);
        volatile unsigned char *m = (volatile unsigned char *)A;
        int zero = 1;
        for (int i = 0; i < 3 * 4096; i++) if (m[i]) zero = 0;
        log_("mmap zero-filled=", zero);
        for (int i = 0; i < 3 * 4096; i++) m[i] = (unsigned char)(i * 7);
        int pat = 1;
        for (int i = 0; i < 3 * 4096; i++)
            if (m[i] != (unsigned char)(i * 7)) pat = 0;
        log_("mmap rw pattern=", pat);

        log_("munmap middle ret=", sys_munmap(A + 4096, 4096));
        long B = sys_mmap(0, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        log_("mmap reuses hole=", B == A + 4096);
        log_("mmap reused page zeroed=", *(volatile unsigned char *)(B + 1) == 0);

        m[5] = 0x5A;
        log_("mprotect none ret=", sys_mprotect(A, 4096, PROT_NONE));
        log_("mprotect rw ret=", sys_mprotect(A, 4096, PROT_READ | PROT_WRITE));
        log_("data survived PROT_NONE=", m[5] == 0x5A);
        log_("mprotect unmapped ret=", sys_mprotect(0x200000000000L, 4096, PROT_READ));

        long fp2 = sys_fork();
        if (fp2 == 0) {
            m[5] = 0x11;
            sys_exit(m[5] == 0x11 ? 21 : 22);
        }
        sys_wait4(fp2, &status, 0);
        log_("mmap fork child exit=", WEXITSTATUS(status));
        log_("mmap fork parent kept=", m[5] == 0x5A);

        long F = sys_mmap(0x200000000000L, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        log_("mmap fixed ok=", F == 0x200000000000L);
        log_("mmap fixed low addr rejected=",
             sys_mmap(0x400000, 4096, PROT_READ,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) < 0);

        long rfd = sys_open("/README", 0);
        char rb[32];
        long rn = sys_read((int)rfd, rb, 32);
        long R = sys_mmap(0, 4096, PROT_READ, MAP_PRIVATE, rfd, 0);
        int same = rn > 0 && R > 0;
        for (int i = 0; same && i < rn; i++)
            if (((volatile char *)R)[i] != rb[i]) same = 0;
        log_("mmap file matches read()=", same);
        sys_close((int)rfd);

        long b0 = sys_brk(0);
        sys_brk(b0 + 8192);
        *(volatile char *)b0 = 9;
        sys_brk(b0);
        sys_brk(b0 + 8192);
        log_("brk regrown page zeroed=", *(volatile char *)b0 == 0);

        int ok = 1;
        for (int i = 0; i < 300; i++) {
            long X = sys_mmap(0, 2 * 1024 * 1024, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (X < 0) { ok = 0; break; }
            *(volatile char *)X = 1;
            sys_munmap(X, 2 * 1024 * 1024);
        }
        log_("mmap/munmap leak loop ok=", ok);
    }

    {
        static char *av[] = {"argtest", "hello", "world", 0};
        static char *ev[] = {"FOO=bar", "X=1", 0};
        long xp = sys_fork();
        if (xp == 0) {
            sys_execve("/bin/argtest", av, ev);
            sys_exit(99);
        }
        sys_wait4(xp, &status, 0);
        log_("execve argtest exit(argc)=", WEXITSTATUS(status));
        log_("execve missing file ret=", sys_execve("/bin/nope", av, ev));
    }

    {
        static unsigned long tlsA[8], tlsB[8];
        tlsA[0] = 111; tlsB[0] = 222;
        long mine_is_child = sys_fork();
        unsigned long *mine = mine_is_child == 0 ? tlsA : tlsB;
        unsigned long want = mine_is_child == 0 ? 111 : 222;
        log_("arch_prctl set_fs ret=", sys_arch_prctl(0x1002, (long)mine));
        long bad = 0;
        for (long i = 0; i < 40000000; i++) {
            unsigned long v;
            __asm__ volatile("mov %%fs:0, %0" : "=r"(v));
            if (v != want) bad++;
        }
        if (mine_is_child == 0) sys_exit(bad ? 1 : 0);
        sys_wait4(mine_is_child, &status, 0);
        log_("fs_base parent mismatches=", bad);
        log_("fs_base child exit(0=ok)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_hello", "arg-one", 0};
        static char *ev[] = {"FOO=from-env", 0};
        long lf = sys_open("/etc/result.log", O_WRONLY | O_APPEND);
        sys_dup2((int)lf, 1);
        sys_close((int)lf);
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_hello", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_hello exit (expect 7)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_fs", 0};
        static char *ev[] = {0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_fs", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_fs exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_fd", 0};
        static char *ev[] = {0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_fd", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_fd exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_time", 0};
        static char *ev[] = {0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_time", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_time exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_io", 0};
        static char *ev[] = {0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_io", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_io exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_sig", 0};
        static char *ev[] = {0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_sig", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_sig exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_sig2", 0};
        static char *ev[] = {0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_sig2", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_sig2 exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_ext2", 0};
        static char *ev[] = {0};
        long me = sys_fork();
        if (me == 0) {
            sys_execve("/bin/musl_ext2", av, ev);
            sys_exit(99);
        }
        sys_wait4(me, &status, 0);
        log_("musl_ext2 exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_ext2w", 0};
        static char *ev[] = {0};
        long mw = sys_fork();
        if (mw == 0) {
            sys_execve("/bin/musl_ext2w", av, ev);
            sys_exit(99);
        }
        sys_wait4(mw, &status, 0);
        log_("musl_ext2w exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_mm", 0};
        static char *ev[] = {0};
        long mm = sys_fork();
        if (mm == 0) {
            sys_execve("/bin/musl_mm", av, ev);
            sys_exit(99);
        }
        sys_wait4(mm, &status, 0);
        log_("musl_mm exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"spie_hello", 0};
        static char *ev[] = {0};
        long mm = sys_fork();
        if (mm == 0) {
            sys_execve("/bin/spie_hello", av, ev);
            sys_exit(99);
        }
        sys_wait4(mm, &status, 0);
        log_("spie_hello exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        long lf = sys_open("/lib/ld-musl-x86_64.so.1", 0);
        if (lf >= 0) {
            sys_close(lf);
            static char *names[] = {"dyn_hello", "dyn_lnk", "dyn_dl"};
            static char *paths[] = {"/bin/dyn_hello", "/bin/dyn_lnk", "/bin/dyn_dl"};
            static char *msgs[] = {"dyn_hello exit (expect 0)=", "dyn_lnk exit (expect 0)=", "dyn_dl exit (expect 0)="};
            for (int di = 0; di < 3; di++) {
                char *av[2] = {names[di], 0};
                static char *ev[] = {0};
                long mm = sys_fork();
                if (mm == 0) {
                    sys_execve(paths[di], av, ev);
                    sys_exit(99);
                }
                sys_wait4(mm, &status, 0);
                log_(msgs[di], WEXITSTATUS(status));
            }
        } else
            log_("dyn tests skipped, no ld-musl on ext2 =", 0);
    }

    {
        static const char *scripts[] = {"/root/tcc_test.sh", "/mnt/sbase_test.sh"};
        static char *smsgs[] = {"tcc_test exit (expect 0)=", "sbase_test exit (expect 0)="};
        for (int si = 0; si < 2; si++) {
            long sf = sys_open(scripts[si], 0);
            if (sf < 0) continue;
            sys_close(sf);
            char *av[3] = {"mksh", (char *)scripts[si], 0};
            static char *ev[] = {"PATH=/bin:/sbin:/usr/bin:/usr/sbin:/mnt/bin", "HOME=/root", 0};
            long mm = sys_fork();
            if (mm == 0) {
                sys_execve("/bin/mksh", av, ev);
                sys_exit(99);
            }
            sys_wait4(mm, &status, 0);
            log_(smsgs[si], WEXITSTATUS(status));
        }
    }

    {
        static char *av[] = {"musl_proc", 0};
        static char *ev[] = {0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_proc", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_proc exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"musl_tty", 0};
        static char *ev[] = {0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/musl_tty", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("musl_tty exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"fastfetch", "-l", "none", "--pipe", 0};
        static char *ev[] = {"HOME=/root", "PATH=/bin", "TERM=dumb", "USER=root", 0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/fastfetch", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("fastfetch exit (expect 0)=", WEXITSTATUS(status));
    }

    {
        static char *av[] = {"mksh", "-c",
            "echo hello; x=$(echo sub); echo \"x=$x\"; for i in 1 2 3; do echo i=$i; done; "
            "cd /etc; pwd; echo data > /etc/mk.txt; read d < /etc/mk.txt; echo \"read=$d\"; "
            "echo a b c | while read p q r; do echo \"$r$q$p\"; done; "
            "false || echo or-ok; (exit 3); echo st=$?; /bin/argtest one two; echo after-ext; exit 7", 0};
        static char *ev[] = {"PATH=/bin:/sbin:/usr/bin:/usr/sbin:/mnt/bin", "HOME=/root", 0};
        long mp = sys_fork();
        if (mp == 0) {
            sys_execve("/bin/mksh", av, ev);
            sys_exit(99);
        }
        sys_wait4(mp, &status, 0);
        log_("mksh -c exit (expect 7)=", WEXITSTATUS(status));
    }

    log_("init: done ", 0);
    sys_exit(0);
}
