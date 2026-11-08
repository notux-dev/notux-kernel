#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/random.h>
#include <sys/prctl.h>
#include <sched.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("sig: %-56s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

static void handler(int s) { (void)s; }

int main(void)
{
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    CHECK(sigaction(SIGUSR1, &sa, &old) == 0 && old.sa_handler == SIG_DFL);
    CHECK(sigaction(SIGUSR1, 0, &old) == 0 && old.sa_handler == handler && (old.sa_flags & SA_RESTART));
    errno = 0; CHECK(sigaction(SIGKILL, &sa, 0) == -1 && errno == EINVAL);
    errno = 0; CHECK(sigaction(SIGSTOP, &sa, 0) == -1 && errno == EINVAL);
    sa.sa_handler = SIG_IGN;
    CHECK(sigaction(SIGUSR2, &sa, 0) == 0);

    sigset_t set, oset;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGKILL);
    CHECK(sigprocmask(SIG_BLOCK, &set, &oset) == 0 && !sigismember(&oset, SIGINT));
    CHECK(sigprocmask(SIG_BLOCK, 0, &oset) == 0 && sigismember(&oset, SIGINT) && !sigismember(&oset, SIGKILL));
    CHECK(sigprocmask(SIG_UNBLOCK, &set, 0) == 0);
    CHECK(sigprocmask(SIG_BLOCK, 0, &oset) == 0 && !sigismember(&oset, SIGINT));
    errno = 0; CHECK(sigprocmask(99, &set, 0) == -1 && errno == EINVAL);

    CHECK(kill(getpid(), 0) == 0);
    errno = 0; CHECK(kill(99999, 0) == -1 && errno == ESRCH);
    errno = 0; CHECK(kill(getpid(), 200) == -1 && errno == EINVAL);

    pid_t p = fork();
    if (p == 0) { for (;;) sched_yield(); }
    int st = 0;
    CHECK(kill(p, SIGTERM) == 0);
    CHECK(waitpid(p, &st, 0) == p && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);

    p = fork();
    if (p == 0) { for (;;) sched_yield(); }
    CHECK(kill(p, SIGKILL) == 0);
    CHECK(waitpid(p, &st, 0) == p && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);

    int pf[2];
    pipe(pf);
    p = fork();
    if (p == 0)
    {
        signal(SIGTERM, SIG_IGN);
        close(pf[0]);
        write(pf[1], "r", 1);
        for (;;) sched_yield();
    }
    close(pf[1]);
    char c;
    CHECK(read(pf[0], &c, 1) == 1);
    CHECK(kill(p, SIGTERM) == 0);
    usleep(100000);
    CHECK(waitpid(p, &st, WNOHANG) == 0);
    kill(p, SIGKILL);
    CHECK(waitpid(p, &st, 0) == p && WTERMSIG(st) == SIGKILL);
    close(pf[0]);

    CHECK(raise(SIGCHLD) == 0);
    CHECK(raise(SIGUSR1) == 0);
    sigset_t pend;
    sigemptyset(&set); sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, 0);
    raise(SIGUSR1);
    sigpending(&pend);
    CHECK(sigismember(&pend, SIGUSR1));
    sigprocmask(SIG_UNBLOCK, &set, 0);

    sa.sa_handler = handler; sa.sa_flags = 0;
    sigaction(SIGCHLD, &sa, 0);
    sigemptyset(&set);
    p = fork();
    if (p == 0) { usleep(100000); _exit(0); }
    errno = 0;
    CHECK(sigsuspend(&set) == -1 && errno == EINTR);
    waitpid(p, &st, 0);

    p = fork();
    if (p == 0) { raise(SIGTERM); _exit(0); }
    CHECK(waitpid(p, &st, 0) == p && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
    p = fork();
    if (p == 0) { abort(); }
    CHECK(waitpid(p, &st, 0) == p && WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT);


    stack_t ss;
    CHECK(sigaltstack(0, &ss) == 0 && (ss.ss_flags & SS_DISABLE));

    struct rlimit rl;
    CHECK(getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur == 8 * 1024 * 1024);
    CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur == 64);
    rl.rlim_cur = 32;
    CHECK(setrlimit(RLIMIT_NOFILE, &rl) == 0);
    CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur == 32 && rl.rlim_max == 64);
    struct rlimit bad_rl = {10, 5};
    errno = 0; CHECK(setrlimit(RLIMIT_NOFILE, &bad_rl) == -1 && errno == EINVAL);
    struct rlimit o2;
    CHECK(prlimit(0, RLIMIT_CORE, 0, &o2) == 0 && o2.rlim_cur == 0);
    errno = 0; CHECK(prlimit(99999, RLIMIT_CORE, 0, &o2) == -1 && errno == ESRCH);

    unsigned char r1[32], r2[32];
    memset(r1, 0, 32); memset(r2, 0, 32);
    CHECK(getrandom(r1, 32, 0) == 32 && getrandom(r2, 32, 0) == 32);
    CHECK(memcmp(r1, r2, 32) != 0);
    int nz = 0;
    for (int i = 0; i < 32; i++) nz += r1[i] != 0;
    CHECK(nz > 10);
    static unsigned char bigbuf[5000];
    CHECK(getrandom(bigbuf, sizeof bigbuf, 0) == 5000);

    struct rusage ru;
    CHECK(getrusage(RUSAGE_SELF, &ru) == 0 && getrusage(RUSAGE_CHILDREN, &ru) == 0);
    CHECK(getuid() == 0 && geteuid() == 0 && getgid() == 0 && getegid() == 0);
    CHECK(setuid(0) == 0 && setgid(0) == 0);
    CHECK(getgroups(0, 0) == 0);
    cpu_set_t cs;
    CHECK(sched_getaffinity(0, sizeof cs, &cs) == 0 && CPU_COUNT(&cs) == 1);
    CHECK(prctl(PR_SET_NAME, "x", 0, 0, 0) == 0);
    CHECK(alarm(5) == 0);

    printf("sig: failures=%d\n", bad);
    return bad;
}
