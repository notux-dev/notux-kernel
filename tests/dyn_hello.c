#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/auxv.h>
#include <link.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("dyn: %-44s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

static int count_cb(struct dl_phdr_info *i, size_t sz, void *d) { (*(int *)d)++; return 0; }

int main(int argc, char **argv)
{
    printf("dyn: hello from a dynamic program, argv0=%s\n", argv[0]);
    CHECK(getauxval(AT_BASE) != 0);
    int n = 0;
    dl_iterate_phdr(count_cb, &n);
    CHECK(n >= 2);
    char *p = malloc(100000);
    CHECK(p != NULL);
    memset(p, 7, 100000);
    free(p);
    FILE *f = fopen("/README", "r");
    CHECK(f != NULL);
    if (f) fclose(f);
    printf("dyn: done, %d failed\n", bad);
    return bad;
}
