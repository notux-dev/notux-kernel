#include <stdio.h>
#include <string.h>

extern int foo_ctor_ran, foo_data, *foo_ptr;
extern const char *foo_name;
extern int foo_add(int, int), foo_len(const char *), foo_tls_bump(void);
extern int foo_call_back(int), foo_has_missing(void);
extern __thread int foo_tls;

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("lnk: %-40s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

int prog_hook(int x) { return x * 10; }

int main(void)
{
    CHECK(foo_ctor_ran == 1);
    CHECK(foo_data == 1234);
    CHECK(*foo_ptr == 99);
    CHECK(strcmp(foo_name, "libfoo") == 0);
    CHECK(foo_add(2, 3) == 5);
    CHECK(foo_len("hello") == 5);
    CHECK(foo_tls == 5 && foo_tls_bump() == 6 && foo_tls == 6);
    CHECK(foo_call_back(4) == 41);
    CHECK(foo_has_missing() == 0);
    printf("lnk: done, %d failed\n", bad);
    return bad;
}
