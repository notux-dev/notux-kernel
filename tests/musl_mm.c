#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("mm: %-46s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)
#define PG 4096UL

int main(void)
{
    unsigned char *a = mmap(0, 4 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(a != MAP_FAILED);
    int z = 1;
    for (unsigned long i = 0; i < 4 * PG; i++) if (a[i]) z = 0;
    CHECK(z);
    for (unsigned long i = 0; i < 4 * PG; i++) a[i] = (unsigned char)(i * 7 + 1);

    unsigned char *b = mremap(a, 4 * PG, 16 * PG, MREMAP_MAYMOVE);
    CHECK(b != MAP_FAILED);
    int keep = 1;
    for (unsigned long i = 0; i < 4 * PG; i++) if (b[i] != (unsigned char)(i * 7 + 1)) keep = 0;
    CHECK(keep);
    z = 1;
    for (unsigned long i = 4 * PG; i < 16 * PG; i++) if (b[i]) z = 0;
    CHECK(z);
    b[16 * PG - 1] = 9;
    CHECK(b[16 * PG - 1] == 9);

    unsigned char *c = mmap(0, 2 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(c != MAP_FAILED);
    c[0] = 0x5a;
    unsigned char *d = mremap(c, 2 * PG, 64 * PG, MREMAP_MAYMOVE);
    CHECK(d != MAP_FAILED && d[0] == 0x5a);
    errno = 0;
    CHECK(mremap(d, 64 * PG, 128 * PG, 0) != MAP_FAILED || errno == ENOMEM);

    unsigned char *e = mremap(b, 16 * PG, 2 * PG, 0);
    CHECK(e == b);
    CHECK(e[PG] == (unsigned char)(PG * 7 + 1));
    CHECK(mremap((void *)0x10000000000UL, PG, 2 * PG, MREMAP_MAYMOVE) == MAP_FAILED);

    CHECK(mprotect(e, PG, PROT_NONE) == 0);
    CHECK(mprotect(e, PG, PROT_READ | PROT_WRITE) == 0);
    CHECK(e[5] == (unsigned char)(5 * 7 + 1));

    errno = 0;
    CHECK(mmap(e, PG, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == MAP_FAILED && errno == EEXIST);
    CHECK(munmap(e, 2 * PG) == 0);
    void *f = mmap(e, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    CHECK(f == (void *)e);

    unsigned char *r = mmap(0, 8 * PG, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(r != MAP_FAILED);
    CHECK(mmap(r, 2 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == (void *)r);
    CHECK(mmap(r + 2 * PG, 3 * PG, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == (void *)(r + 2 * PG));
    r[0] = 1; r[2 * PG - 1] = 2;
    CHECK(r[0] == 1 && r[2 * PG - 1] == 2);

    int fd = open("/README", O_RDONLY);
    CHECK(fd >= 0);
    char ref[64] = {0};
    CHECK(pread(fd, ref, 32, 16) == 32);
    unsigned char *m = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    CHECK(m != MAP_FAILED);
    CHECK(memcmp(m + 16, ref, 32) == 0);
    m[16] ^= 0xff;
    char again[2] = {0};
    CHECK(pread(fd, again, 1, 16) == 1 && again[0] == ref[0]);
    errno = 0;
    CHECK(mmap(0, PG, PROT_READ, MAP_PRIVATE, fd, 3) == MAP_FAILED && errno == EINVAL);
    close(fd);
    int dfd = open("/bin", O_RDONLY);
    errno = 0;
    CHECK(dfd >= 0 && mmap(0, PG, PROT_READ, MAP_PRIVATE, dfd, 0) == MAP_FAILED && errno == ENODEV);
    if (dfd >= 0) close(dfd);
    errno = 0;
    CHECK(mmap(0, PG, PROT_READ, MAP_PRIVATE, 99, 0) == MAP_FAILED && errno == EBADF);

    enum { N = 200 };
    unsigned char *big = mmap(0, 2 * N * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(big != MAP_FAILED);
    int cut_ok = 1;
    for (int i = 0; i < N; i++)
        if (munmap(big + (2 * i + 1) * PG, PG) != 0) cut_ok = 0;
    CHECK(cut_ok);
    int touch_ok = 1;
    for (int i = 0; i < N; i++) big[2 * i * PG] = (unsigned char)i;
    for (int i = 0; i < N; i++) if (big[2 * i * PG] != (unsigned char)i) touch_ok = 0;
    CHECK(touch_ok);
    int more = 1;
    for (int i = 0; i < 20; i++)
        if (mmap(0, PG, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) more = 0;
    CHECK(more || errno == ENOMEM);

    unsigned char vec[2];
    CHECK(mincore(big, PG, vec) == 0 && vec[0] == 1);
    CHECK(msync(big, PG, MS_SYNC) == 0);
    CHECK(mlock(big, PG) == 0 && munlock(big, PG) == 0);

    unsigned char *p = malloc(300000);
    CHECK(p != NULL);
    memset(p, 0xab, 300000);
    p = realloc(p, 3000000);
    CHECK(p != NULL && p[0] == 0xab && p[299999] == 0xab);
    memset(p + 300000, 0xcd, 2700000);
    p = realloc(p, 1000000);
    CHECK(p != NULL && p[299999] == 0xab && p[300000] == 0xcd);
    free(p);

    unsigned char *s = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    s[0] = 11;
    pid_t k = fork();
    if (k == 0) { s[0] = 22; _exit(s[0] == 22 ? 0 : 1); }
    int st = 0;
    waitpid(k, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0 && s[0] == 11);

    printf("mm: done, %d failed\n", bad);
    return bad;
}
