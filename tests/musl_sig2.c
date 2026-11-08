#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("sig2: %-56s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

static volatile int got, got_info, chld;
static volatile double fx;
static void h1(int s, siginfo_t *si, void *uc)
{
    (void)uc; got = s; got_info = si ? si->si_signo : -1;
    fx = 12345.678;
}
static void hchld(int s) { (void)s; chld++; }
static void halrm(int s) { (void)s; got = s; }

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = h1; sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, 0);

    volatile double a = 1.5, b = 2.25;
    kill(getpid(), SIGUSR1);
    CHECK(got == SIGUSR1 && got_info == SIGUSR1);
    CHECK(a * b == 3.375);

    got = 0;
    sigset_t m, o, p;
    sigemptyset(&m); sigaddset(&m, SIGUSR1);
    sigprocmask(SIG_BLOCK, &m, &o);
    kill(getpid(), SIGUSR1);
    CHECK(got == 0);
    sigpending(&p);
    CHECK(sigismember(&p, SIGUSR1));
    sigprocmask(SIG_SETMASK, &o, 0);
    CHECK(got == SIGUSR1);

    got = 0;
    sa.sa_handler = halrm; sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, 0);
    alarm(1);
    errno = 0;
    int r = pause();
    CHECK(r == -1 && errno == EINTR && got == SIGALRM);

    int fd[2]; pipe(fd);
    got = 0;
    alarm(1);
    char c;
    errno = 0;
    r = read(fd[0], &c, 1);
    CHECK(r == -1 && errno == EINTR && got == SIGALRM);

    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, 0);
    pid_t k = fork();
    if (k == 0) { usleep(300000); write(fd[1], "x", 1); _exit(0); }
    got = 0;
    alarm(0);
    r = alarm(1); (void)r;
    r = read(fd[0], &c, 1);
    CHECK(r == 1 && c == 'x');
    waitpid(k, 0, 0);
    alarm(0);

    sa.sa_handler = hchld; sa.sa_flags = 0;
    sigaction(SIGCHLD, &sa, 0);
    sigemptyset(&m); sigaddset(&m, SIGCHLD);
    sigprocmask(SIG_BLOCK, &m, &o);
    k = fork();
    if (k == 0) _exit(3);
    sigset_t empty; sigemptyset(&empty);
    chld = 0;
    while (!chld) sigsuspend(&empty);
    CHECK(chld == 1);
    int st = 0;
    CHECK(waitpid(k, &st, 0) == k && WEXITSTATUS(st) == 3);
    sigprocmask(SIG_SETMASK, &o, 0);
    sigpending(&p);
    CHECK(!sigismember(&p, SIGCHLD));
    sigprocmask(SIG_BLOCK, 0, &p);
    CHECK(!sigismember(&p, SIGCHLD));

    k = fork();
    if (k == 0) { int q[2]; pipe(q); close(q[0]); write(q[1], "a", 1); _exit(0); }
    waitpid(k, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGPIPE);
    k = fork();
    if (k == 0) { signal(SIGPIPE, SIG_IGN); int q[2]; pipe(q); close(q[0]); _exit(write(q[1], "a", 1) == -1 && errno == EPIPE ? 0 : 1); }
    waitpid(k, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);

    k = fork();
    if (k == 0) { volatile long addr = 0x700000000000L; *(volatile int *)addr = 1; _exit(0); }
    waitpid(k, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);

    k = fork();
    if (k == 0) { for (;;) pause(); }
    usleep(100000);
    kill(k, SIGTERM);
    waitpid(k, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);

    printf("sig2: done, %d failed\n", bad);
    return bad;
}
