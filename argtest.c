typedef unsigned long u64;

static inline long sc3(long n, long a, long b, long c)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory", "cc");
    return r;
}
static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static int fmt(long v, char *o)
{
    char t[24]; int n = 0, l = 0; int neg = v < 0;
    u64 u = neg ? (u64)(-v) : (u64)v;
    if (!u) t[n++] = '0';
    while (u) { t[n++] = '0' + u % 10; u /= 10; }
    if (neg) o[l++] = '-';
    while (n) o[l++] = t[--n];
    o[l] = 0; return l;
}
static void logs(const char *m, const char *s)
{
    long fd = sc3(2, (long)"/etc/result.log", 0x0001 | 0x0400, 0);
    if (fd < 0) return;
    sc3(1, fd, (long)m, (long)slen(m));
    sc3(1, fd, (long)s, (long)slen(s));
    sc3(1, fd, (long)"\n", 1);
    sc3(3, fd, 0, 0);
}
static void logv(const char *m, long v)
{
    char b[24]; fmt(v, b); logs(m, b);
}

void cmain(long *sp, long entry_rsp_mod16)
{
    long argc = sp[0];
    char **argv = (char **)(sp + 1);
    char **envp = argv + argc + 1;
    char **e = envp; while (*e) e++;
    u64 *aux = (u64 *)(e + 1);

    logv("argtest: rsp%16=", entry_rsp_mod16);
    logv("argtest: argc=", argc);
    for (long i = 0; i < argc; i++) logs("argtest: argv=", argv[i]);
    for (char **p = envp; *p; p++) logs("argtest: env=", *p);
    u64 pagesz = 0, phnum = 0, phdr = 0, entry = 0, rnd = 0, execfn = 0;
    for (; aux[0]; aux += 2) {
        if (aux[0] == 6) pagesz = aux[1];
        if (aux[0] == 5) phnum = aux[1];
        if (aux[0] == 3) phdr = aux[1];
        if (aux[0] == 9) entry = aux[1];
        if (aux[0] == 25) rnd = aux[1];
        if (aux[0] == 31) execfn = aux[1];
    }
    logv("argtest: AT_PAGESZ=", (long)pagesz);
    logv("argtest: AT_PHNUM>0=", phnum > 0);
    logv("argtest: AT_PHDR first p_type=", phdr ? *(unsigned *)phdr : -1);
    logv("argtest: AT_ENTRY>0x400000=", entry >= 0x400000);
    int nz = 0;
    if (rnd) for (int i = 0; i < 16; i++) if (((unsigned char *)rnd)[i]) nz = 1;
    logv("argtest: AT_RANDOM nonzero=", nz);
    if (execfn) logs("argtest: AT_EXECFN=", (const char *)execfn);
    sc3(60, argc, 0, 0);
    for (;;) {}
}

__asm__(".globl _start\n_start:\n"
        "  mov %rsp, %rdi\n"
        "  mov %rsp, %rsi\n"
        "  and $15, %rsi\n"
        "  and $-16, %rsp\n"
        "  call cmain\n");
