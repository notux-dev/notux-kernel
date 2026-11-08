#include "syscalls.h"
#include "kfile.h"
#include "process.h"
#include "vmm.h"
#include "pmm.h"
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "Xjail/xv6/xv6fs_api.h"


extern int load_elf_bias(process_t *proc, const uint8_t *file,
                         uint64_t file_size, uint64_t bias);

#define PIE_BASE    0x4000000000ULL
#define INTERP_BASE 0x8000000000ULL
#define PT_LOAD_  1
#define PT_INTERP_ 3
#ifndef ELOOP
#define ELOOP 40
#endif
#define ET_EXEC_  2
#define ET_DYN_   3

#define MAX_ELF (16 * 1024 * 1024)
#define EXEC_STACK_SIZE (128 * 1024)
#define MAX_ARGS 64
#define STR_BUF  32768
#define USER_MAX 0x7FFFFFFFFFFFULL

typedef struct { uint8_t e_ident[16]; uint16_t e_type, e_machine;
    uint32_t e_version; uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags; uint16_t e_ehsize, e_phentsize, e_phnum,
    e_shentsize, e_shnum, e_shstrndx; } ehdr_t;
typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr,
    p_paddr, p_filesz, p_memsz, p_align; } phdr_t;

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_FLAGS  8
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_PLATFORM 15
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_EXECFN 31

static char strbuf[STR_BUF];
static uint64_t stk_phys[EXEC_STACK_SIZE / PAGE_SIZE];

#define RET(v) do { regs[14] = (uint64_t)(int64_t)(v); return (uint64_t)regs; } while (0)

static int uread(process_t *p, uint64_t va, void *dst, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n)
    {
        if (va > USER_MAX || !vmm_virt_to_phys(p->pml4, va))
            return -1;
        uint64_t chunk = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (chunk > n)
            chunk = n;
        const uint8_t *s = (const uint8_t *)vmm_virt_to_phys(p->pml4, va);
        for (uint64_t i = 0; i < chunk; i++)
            d[i] = s[i];
        d += chunk; va += chunk; n -= chunk;
    }
    return 0;
}

static int ustr(process_t *p, uint64_t va, int *off)
{
    int start = *off;
    for (;;)
    {
        char c;
        if (*off >= STR_BUF || uread(p, va++, &c, 1) < 0)
            return -1;
        strbuf[(*off)++] = c;
        if (!c)
            return start;
    }
}

static void sput(uint64_t va, const void *src, uint64_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    uint64_t base = USER_STACK_TOP - EXEC_STACK_SIZE;
    while (n)
    {
        uint64_t idx = (va - base) / PAGE_SIZE, po = (va - base) % PAGE_SIZE;
        uint64_t chunk = PAGE_SIZE - po;
        if (chunk > n)
            chunk = n;
        uint8_t *d = (uint8_t *)stk_phys[idx] + po;
        for (uint64_t i = 0; i < chunk; i++)
            d[i] = s[i];
        s += chunk; va += chunk; n -= chunk;
    }
}
static void sput64(uint64_t va, uint64_t v) { sput(va, &v, 8); }

static uint64_t slen(const char *s) { uint64_t n = 0; while (s[n]) n++; return n; }

static int read_whole(const char *path, uint8_t **buf, uint32_t *npg_out,
                      uint32_t *size_out)
{
    struct inode *ip = namei((char *)path);
    if (!ip)
        return -ENOENT;
    ilock(ip);
    uint32_t size = ip->size;
    if (size < 2 || size > MAX_ELF)
    {
        iunlock(ip); iput(ip);
        return -ENOEXEC;
    }
    uint32_t npg = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *b = (uint8_t *)pmm_alloc_contig(npg);
    if (!b)
    {
        iunlock(ip); iput(ip);
        return -ENOMEM;
    }
    int got = readi(ip, (char *)b, 0, size);
    iunlock(ip); iput(ip);
    if (got != (int)size)
    {
        pmm_free_contig(b, npg);
        return -ENOEXEC;
    }
    *buf = b; *npg_out = npg; *size_out = size;
    return 0;
}

#define FAILX(v) do { pmm_free_contig(elf_buf, npg); \
    if (ibuf) pmm_free_contig(ibuf, inpg); RET(v); } while (0)

uint64_t sys_execve(uint64_t *regs)
{
    process_t *proc = get_current_process();
    uint64_t upath = regs[9], uargv = regs[10], uenvp = regs[11];

    int off = 0, path_off, argc = 0, envc = 0;
    int argo[MAX_ARGS], envo[MAX_ARGS];
    if ((path_off = ustr(proc, upath, &off)) < 0)
        RET(-EFAULT);
    for (int pass = 0; pass < 2; pass++)
    {
        uint64_t arr = pass ? uenvp : uargv;
        if (!arr)
            continue;
        for (;;)
        {
            uint64_t sp;
            if (uread(proc, arr, &sp, 8) < 0)
                RET(-EFAULT);
            arr += 8;
            if (!sp)
                break;
            if ((pass ? envc : argc) >= MAX_ARGS)
                RET(-E2BIG);
            int o = ustr(proc, sp, &off);
            if (o < 0)
                RET(-E2BIG);
            if (pass) envo[envc++] = o; else argo[argc++] = o;
        }
    }

    uint8_t *elf_buf = 0, *ibuf = 0;
    uint32_t npg = 0, inpg = 0, size = 0, isize = 0;
    int shebang_depth = 0;
reread:
    {
        int rr = read_whole(strbuf + path_off, &elf_buf, &npg, &size);
        if (rr < 0)
            RET(rr);
    }
    if (size >= 2 && elf_buf[0] == '#' && elf_buf[1] == '!')
    {
        if (++shebang_depth > 4)
            FAILX(-ELOOP);
        char line[256];
        int n = 0;
        for (uint32_t i = 2; i < size && i < sizeof line + 1 && elf_buf[i] != '\n'; i++)
            line[n++] = (char)elf_buf[i];
        line[n] = 0;
        int a = 0;
        while (line[a] == ' ' || line[a] == '\t') a++;
        int b = a;
        while (line[b] && line[b] != ' ' && line[b] != '\t') b++;
        int c = b;
        if (line[c]) { line[c++] = 0; while (line[c] == ' ' || line[c] == '\t') c++; }
        int e = n;
        while (e > c && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) line[--e] = 0;
        if (line[a] == 0)
            FAILX(-ENOEXEC);
        pmm_free_contig(elf_buf, npg);
        elf_buf = 0;
        int script_off = path_off, newargo[MAX_ARGS], na = 0;
        int interp_off = off;
        for (char *q = line + a;; q++) { if (off >= STR_BUF) RET(-E2BIG); strbuf[off++] = *q; if (!*q) break; }
        newargo[na++] = interp_off;
        if (line[c])
        {
            int o = off;
            for (char *q = line + c;; q++) { if (off >= STR_BUF) RET(-E2BIG); strbuf[off++] = *q; if (!*q) break; }
            newargo[na++] = o;
        }
        newargo[na++] = script_off;
        for (int i = 1; i < argc && na < MAX_ARGS; i++)
            newargo[na++] = argo[i];
        for (int i = 0; i < na; i++)
            argo[i] = newargo[i];
        argc = na;
        path_off = interp_off;
        goto reread;
    }
    ehdr_t *eh = (ehdr_t *)elf_buf;
    if (size < sizeof(ehdr_t) || eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != 2 || eh->e_machine != 62 ||
        (eh->e_type != ET_EXEC_ && eh->e_type != ET_DYN_) ||
        eh->e_phentsize != sizeof(phdr_t) ||
        eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(phdr_t) > size)
        FAILX(-ENOEXEC);
    phdr_t *ph = (phdr_t *)(elf_buf + eh->e_phoff);
    uint64_t exe_bias = (eh->e_type == ET_DYN_) ? PIE_BASE : 0;

    uint64_t entry = eh->e_entry + exe_bias;
    uint64_t interp_base = 0;
    for (int i = 0; i < eh->e_phnum; i++)
    {
        if (ph[i].p_type != PT_INTERP_)
            continue;
        char ipath[256];
        if (ph[i].p_filesz < 2 || ph[i].p_filesz > sizeof ipath ||
            ph[i].p_offset + ph[i].p_filesz > size)
            FAILX(-ENOEXEC);
        for (uint64_t k = 0; k < ph[i].p_filesz; k++)
            ipath[k] = (char)elf_buf[ph[i].p_offset + k];
        if (ipath[ph[i].p_filesz - 1] != 0)
            FAILX(-ENOEXEC);
        int rr = read_whole(ipath, &ibuf, &inpg, &isize);
        if (rr < 0)
            FAILX(rr);
        ehdr_t *ie = (ehdr_t *)ibuf;
        if (ie->e_ident[0] != 0x7F || ie->e_ident[1] != 'E' ||
            ie->e_ident[2] != 'L' || ie->e_ident[3] != 'F' ||
            ie->e_ident[4] != 2 || ie->e_machine != 62 ||
            ie->e_type != ET_DYN_ || ie->e_phentsize != sizeof(phdr_t) ||
            ie->e_phoff + (uint64_t)ie->e_phnum * sizeof(phdr_t) > isize)
            FAILX(-ELIBBAD);
        interp_base = INTERP_BASE;
        entry = ie->e_entry + interp_base;
        break;
    }

    uint64_t phdr_va = 0;
    for (int i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == 6)
            phdr_va = ph[i].p_vaddr;
    if (!phdr_va)
        for (int i = 0; i < eh->e_phnum; i++)
            if (ph[i].p_type == 1 && eh->e_phoff >= ph[i].p_offset &&
                eh->e_phoff < ph[i].p_offset + ph[i].p_filesz)
                phdr_va = ph[i].p_vaddr + (eh->e_phoff - ph[i].p_offset);

    uint64_t *oldpml4 = proc->pml4;
    uint64_t *np = create_user_pml4();
    if (!np)
        FAILX(-ENOMEM);
    proc->pml4 = np;
    if (load_elf_bias(proc, elf_buf, size, exe_bias) < 0 ||
        (ibuf && load_elf_bias(proc, ibuf, isize, interp_base) < 0))
    {
        proc->pml4 = oldpml4;
        vmm_free_user_space(np);
        FAILX(-ENOEXEC);
    }
    uint64_t sbase = USER_STACK_TOP - EXEC_STACK_SIZE;
    for (uint64_t i = 0; i < EXEC_STACK_SIZE / PAGE_SIZE; i++)
    {
        uint8_t *pg = (uint8_t *)pmm_alloc_page();
        if (!pg)
        {
            proc->pml4 = oldpml4;
            vmm_free_user_space(np);
            FAILX(-ENOMEM);
        }
        for (int b = 0; b < PAGE_SIZE; b++)
            pg[b] = 0;
        stk_phys[i] = (uint64_t)pg;
        vmm_map_page(np, sbase + i * PAGE_SIZE, (uint64_t)pg,
                     PAGE_USER | PAGE_WRITE | PAGE_OWNED);
    }

    uint64_t sp = USER_STACK_TOP;
    uint64_t execfn = 0, argva[MAX_ARGS], envva[MAX_ARGS];
    #define PUSHSTR(dst, s) do { uint64_t l_ = slen(s) + 1; sp -= l_; \
        sput(sp, s, l_); dst = sp; } while (0)
    PUSHSTR(execfn, strbuf + path_off);
    for (int i = argc - 1; i >= 0; i--) PUSHSTR(argva[i], strbuf + argo[i]);
    for (int i = envc - 1; i >= 0; i--) PUSHSTR(envva[i], strbuf + envo[i]);
    uint64_t platform; PUSHSTR(platform, "x86_64");
    uint8_t rnd[16];
    uint64_t t0, t1;
    { uint32_t lo_, hi_; __asm__ volatile("rdtsc" : "=a"(lo_), "=d"(hi_));
      t0 = ((uint64_t)hi_ << 32) | lo_; }
    t1 = t0 * 6364136223846793005ULL + 1442695040888963407ULL;
    for (int i = 0; i < 8; i++) { rnd[i] = (uint8_t)(t0 >> (i * 8));
                                  rnd[8 + i] = (uint8_t)(t1 >> (i * 8)); }
    sp -= 16; sput(sp, rnd, 16);
    uint64_t random_va = sp;
    sp &= ~15ULL;

    uint64_t aux[][2] = {
        {AT_PHDR, phdr_va ? phdr_va + exe_bias : 0}, {AT_PHENT, sizeof(phdr_t)},
        {AT_PHNUM, eh->e_phnum}, {AT_PAGESZ, PAGE_SIZE},
        {AT_BASE, interp_base},
        {AT_FLAGS, 0}, {AT_ENTRY, eh->e_entry + exe_bias}, {AT_UID, 0}, {AT_EUID, 0},
        {AT_GID, 0}, {AT_EGID, 0}, {AT_PLATFORM, platform},
        {AT_CLKTCK, 100}, {AT_SECURE, 0}, {AT_RANDOM, random_va},
        {AT_EXECFN, execfn}, {AT_NULL, 0},
    };
    int naux = (int)(sizeof(aux) / sizeof(aux[0]));
    uint64_t nwords = 1 + (argc + 1) + (envc + 1) + 2 * (uint64_t)naux;
    uint64_t top = (sp - nwords * 8) & ~15ULL;
    if (USER_STACK_TOP - top > EXEC_STACK_SIZE - PAGE_SIZE)
    {
        proc->pml4 = oldpml4;
        vmm_free_user_space(np);
        FAILX(-E2BIG);
    }
    uint64_t w = top;
    sput64(w, (uint64_t)argc); w += 8;
    uint64_t argv_va = w;
    for (int i = 0; i < argc; i++) { sput64(w, argva[i]); w += 8; }
    sput64(w, 0); w += 8;
    for (int i = 0; i < envc; i++) { sput64(w, envva[i]); w += 8; }
    sput64(w, 0); w += 8;
    for (int i = 0; i < naux; i++) { sput64(w, aux[i][0]); w += 8;
                                     sput64(w, aux[i][1]); w += 8; }

    pmm_free_contig(elf_buf, npg);
    if (ibuf)
        pmm_free_contig(ibuf, inpg);
    vmm_switch_pml4(np);
    vmm_free_user_space(oldpml4);
    proc->heap_start = 0;
    proc->heap_end = 0;
    proc->nvmas = 0;
    proc->fs_base = 0;
    fdt_close_cloexec(proc);
    proc->tmo_active = 0;
    for (int s_ = 0; s_ < NSIG_; s_++)
        if (proc->sigact[s_].handler != 1)
            proc->sigact[s_] = (ksigaction_t){0, 0, 0, 0};
    proc->sigpending = 0;
    {
        const char *pth = strbuf + path_off, *base = pth;
        for (const char *q = pth; *q; q++)
            if (*q == '/' && q[1])
                base = q + 1;
        int ci = 0;
        while (base[ci] && base[ci] != '/' && ci < 15)
        {
            proc->comm[ci] = base[ci];
            ci++;
        }
        proc->comm[ci] = 0;
    }
    proc->restart_frame = 0;
    proc->sigsusp = 0;
    proc->alarm_tick = 0;
    proc->alarm_interval = 0;
    process_fpu_reset_current();
    __asm__ volatile("wrmsr" : : "c"(0xC0000100u), "a"(0), "d"(0));

    for (int i = 0; i < 15; i++)
        regs[i] = 0;
    regs[9]  = (uint64_t)argc;
    regs[10] = argv_va;
    regs[15] = entry;
    regs[16] = 0x23;
    regs[17] = 0x202;
    regs[18] = top;
    regs[19] = 0x1B;
    return (uint64_t)regs;
}
