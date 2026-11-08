# tcc check: compiles and runs small C programs on notux itself (run: mksh /root/tcc_test.sh)
PATH=/usr/bin:/bin
W=/tmp/tcct
rm -rf $W; mkdir -p $W; cd $W
fail=0
chk() { if [ "$2" = "$3" ]; then echo "tcc: $1 ok"; else echo "tcc: $1 FAIL (got '$2', want '$3')"; fail=$((fail+1)); fi; }

cat > hello.c <<'C'
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    char *p = malloc(64);
    strcpy(p, "hello from tcc on notux");
    printf("%s (%d args)\n", p, argc - 1);
    free(p);
    return 3;
}
C
tcc -o hello hello.c;               chk compile-dynamic "$?" 0
./hello a b >out.txt; rc=$?;        chk run-dynamic-rc "$rc" 3
chk run-dynamic-out "$(cat out.txt)" "hello from tcc on notux (2 args)"
tcc -static -o hello_s hello.c;     chk compile-static "$?" 0
./hello_s x >out2.txt; rc=$?;       chk run-static-rc "$rc" 3
chk run-static-out "$(cat out2.txt)" "hello from tcc on notux (1 args)"

cat > sq.c <<'C'
int square(int x) { return x * x; }
C
cat > main2.c <<'C'
#include <stdio.h>
int square(int);
int main(void) { printf("%d\n", square(12)); return 0; }
C
tcc -c sq.c && tcc -c main2.c && tcc -o two sq.o main2.o; chk separate-compile "$?" 0
chk link-two-objects "$(./two)" 144

tcc -run hello.c q >/dev/null; chk run-in-memory "$?" 3

cat > lib.c <<'C'
int twice(int x) { return 2 * x; }
C
tcc -shared -o libtwice.so lib.c;   chk shared-lib "$?" 0
cat > usel.c <<'C'
#include <stdio.h>
#include <dlfcn.h>
int main(void) {
    void *h = dlopen("/tmp/tcct/libtwice.so", RTLD_NOW);
    if (!h) { printf("dlopen failed\n"); return 1; }
    int (*f)(int) = dlsym(h, "twice");
    printf("%d\n", f(21));
    return 0;
}
C
tcc -o usel usel.c;                 chk compile-dlopen "$?" 0
chk dlopen-tcc-lib "$(./usel)" 42

# a bigger program: the tcc compiler compiles one of the sbase tools
cat > fib.c <<'C'
#include <stdio.h>
static long fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main(void) { printf("%ld\n", fib(24)); return 0; }
C
tcc -o fib fib.c;                   chk compile-fib "$?" 0
chk run-fib "$(./fib)" 46368

echo "tcc: done, $fail failed"
exit $fail
