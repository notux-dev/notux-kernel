#include <string.h>
#include <stdlib.h>

int foo_ctor_ran;
int foo_data = 1234;
static int hidden = 99;
int *foo_ptr = &hidden;
const char *foo_name = "libfoo";
__thread int foo_tls = 5;

__attribute__((constructor)) static void foo_init(void) { foo_ctor_ran = 1; }

int foo_add(int a, int b) { return a + b + (*foo_ptr - 99); }
int foo_len(const char *s) { return (int)strlen(s); }
int foo_tls_bump(void) { return ++foo_tls; }
extern int prog_hook(int);
int foo_call_back(int x) { return prog_hook(x) + 1; }
extern int maybe_missing(void) __attribute__((weak));
int foo_has_missing(void) { return maybe_missing != 0; }
