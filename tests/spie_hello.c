#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <elf.h>
#include <link.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("spie: %-40s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

static int global = 41;
static int *gp = &global;
static const char *msg = "relocated string";

int main(int argc, char **argv)
{
    printf("spie: main at %p, argv0=%s\n", (void *)main, argv[0]);
    CHECK((unsigned long)main >= 0x4000000000UL);
    CHECK(*gp == 41 && gp == &global);
    CHECK(strcmp(msg, "relocated string") == 0);
    CHECK(getauxval(AT_BASE) == 0);
    CHECK(getauxval(AT_PAGESZ) == 4096);
    CHECK(getauxval(AT_PHNUM) >= 3);
    const Elf64_Phdr *ph = (const Elf64_Phdr *)getauxval(AT_PHDR);
    CHECK(ph != 0 && ph->p_type != 0);
    printf("spie: done, %d failed\n", bad);
    return bad;
}
