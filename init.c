#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
    puts("hello from musl init");
    pid_t p = fork();
    if (p == 0) {
        char *av[] = {"child", 0};
        char *ev[] = {"PATH=/bin", 0};
        execve("/bin/child", av, ev);
        _exit(127);
    }
    for (;;) {
        int st;
        if (wait4(-1, &st, 0, 0) < 0)
            sleep(1);
    }
}
