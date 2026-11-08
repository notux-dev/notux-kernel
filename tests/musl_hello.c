#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    printf("musl: argc=%d argv1=%s FOO=%s\n", argc, argc > 1 ? argv[1] : "-",
           getenv("FOO") ? getenv("FOO") : "(unset)");
    char *p = malloc(300000);
    memset(p, 'x', 300000);
    printf("musl: malloc+memset ok, len=%zu pid>0=%d\n", strlen("abc"),
           getpid() > 0);
    snprintf(p, 64, "%5.2f|%-4d|%x", 3.14159, 42, 255);
    printf("musl: fmt=%s\n", p);
    free(p);
    return 7;
}
