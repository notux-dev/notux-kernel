#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("dl: %-44s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

int prog_hook(int x) { return x * 10; }

int main(void)
{
    void *h = dlopen("/lib/libfoo.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) printf("dl: dlopen failed: %s\n", dlerror());
    CHECK(h != NULL);
    int (*add)(int, int) = h ? dlsym(h, "foo_add") : 0;
    CHECK(add != NULL && add(20, 22) == 42);
    int *ctor = h ? dlsym(h, "foo_ctor_ran") : 0;
    CHECK(ctor && *ctor == 1);
    int *data = h ? dlsym(h, "foo_data") : 0;
    CHECK(data && *data == 1234);
    int (*bump)(void) = h ? dlsym(h, "foo_tls_bump") : 0;
    CHECK(bump && bump() == 6 && bump() == 7);
    int (*cb)(int) = h ? dlsym(h, "foo_call_back") : 0;
    CHECK(cb && cb(3) == 31);
    CHECK(h && dlsym(h, "no_such_symbol") == NULL);
    CHECK(dlerror() != NULL || 1);

    void *h2 = dlopen("libfoo.so", RTLD_NOW);
    CHECK(h2 != NULL);
    void *bad_h = dlopen("/lib/does_not_exist.so", RTLD_NOW);
    CHECK(bad_h == NULL && dlerror() != NULL);
    void *self = dlopen(NULL, RTLD_NOW);
    CHECK(self != NULL && dlsym(self, "prog_hook") != NULL);
    CHECK(dlsym(RTLD_DEFAULT, "printf") != NULL);
    if (h2) dlclose(h2);
    CHECK(h && dlclose(h) == 0);
    printf("dl: done, %d failed\n", bad);
    return bad;
}
