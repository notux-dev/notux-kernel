#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <poll.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("tty: %-56s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

static void sti(const char *s) { for (; *s; s++) ioctl(0, TIOCSTI, s); }
static volatile int got_int;
static void on_int(int s) { (void)s; got_int++; }

int main(void)
{
    struct termios t, orig;
    CHECK(isatty(0));
    CHECK(tcgetattr(0, &orig) == 0 && (orig.c_lflag & ICANON) && (orig.c_lflag & ECHO) && (orig.c_iflag & ICRNL));
    CHECK(orig.c_cc[VINTR] == 3 && orig.c_cc[VERASE] == 127 && orig.c_cc[VEOF] == 4 && orig.c_cc[VMIN] == 1);
    struct winsize ws;
    CHECK(ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40 && ws.ws_row >= 10);
    int pipefd[2]; pipe(pipefd);
    CHECK(!isatty(pipefd[0]));
    errno = 0; CHECK(tcgetattr(pipefd[0], &t) == -1 && errno == ENOTTY);

    t = orig; t.c_lflag &= ~ECHO;
    CHECK(tcsetattr(0, TCSANOW, &t) == 0);
    int fl = fcntl(0, F_GETFL); fcntl(0, F_SETFL, fl | O_NONBLOCK);
    char b[64];
    errno = 0; CHECK(read(0, b, sizeof b) == -1 && errno == EAGAIN);
    sti("abc");
    errno = 0; CHECK(read(0, b, sizeof b) == -1 && errno == EAGAIN);
    sti("\x7f" "d\r");
    int n = read(0, b, sizeof b);
    CHECK(n == 4 && !memcmp(b, "abd\n", 4));
    sti("xy\x04");
    n = read(0, b, sizeof b);
    CHECK(n == 2 && !memcmp(b, "xy", 2));
    sti("\x04");
    CHECK(read(0, b, sizeof b) == 0);
    sti("hello\x15" "bye\r");
    n = read(0, b, sizeof b);
    CHECK(n == 4 && !memcmp(b, "bye\n", 4));
    sti("one\rtwo\r");
    n = read(0, b, 2); CHECK(n == 2 && !memcmp(b, "on", 2));
    n = read(0, b, sizeof b); CHECK(n == 2 && !memcmp(b, "e\n", 2));
    n = read(0, b, sizeof b); CHECK(n == 4 && !memcmp(b, "two\n", 4));
    struct pollfd pf = { 0, POLLIN, 0 };
    CHECK(poll(&pf, 1, 0) == 0);
    sti("z\r");
    CHECK(poll(&pf, 1, 0) == 1 && (pf.revents & POLLIN));
    int avail = 0; CHECK(ioctl(0, FIONREAD, &avail) == 0 && avail == 2);
    read(0, b, sizeof b);

    t.c_lflag &= ~(ICANON | ISIG);
    t.c_iflag &= ~ICRNL;
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    CHECK(tcsetattr(0, TCSANOW, &t) == 0);
    sti("q");
    CHECK(read(0, b, sizeof b) == 1 && b[0] == 'q');
    sti("a\rb");
    n = read(0, b, sizeof b);
    CHECK(n == 3 && !memcmp(b, "a\rb", 3));
    fcntl(0, F_SETFL, fl);

    pid_t k = fork();
    if (k == 0) { usleep(200000); char c = 'k'; ioctl(0, TIOCSTI, &c); _exit(0); }
    n = read(0, b, 1);
    CHECK(n == 1 && b[0] == 'k');
    waitpid(k, 0, 0);

    t = orig; t.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &t);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_int;
    sigaction(SIGINT, &sa, 0);
    CHECK(tcgetpgrp(0) == getpgrp());
    got_int = 0;
    sti("\x03");
    CHECK(got_int == 1);
    sigaction(SIGINT, &(struct sigaction){ .sa_handler = SIG_DFL }, 0);

    k = fork();
    if (k == 0)
    {
        int ok = getpgrp() == getppid() ? 1 : getpgrp() > 0;
        ok &= setpgid(0, 0) == 0 && getpgrp() == getpid();
        ok &= setsid() == -1 && errno == EPERM;
        _exit(ok ? 0 : 1);
    }
    int st; waitpid(k, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    k = fork();
    if (k == 0)
    {
        pid_t s = setsid();
        _exit(s == getpid() && getsid(0) == getpid() && getpgrp() == getpid() ? 0 : 1);
    }
    waitpid(k, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    k = fork();
    if (k == 0) { setpgid(0, 0); for (;;) pause(); }
    usleep(100000);
    CHECK(killpg(k, SIGTERM) == 0);
    waitpid(k, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);

    struct stat sb;
    int fd = open("/dev/null", O_RDWR);
    CHECK(fd >= 0 && write(fd, "xyz", 3) == 3 && read(fd, b, 8) == 0);
    CHECK(fstat(fd, &sb) == 0 && S_ISCHR(sb.st_mode) && major(sb.st_rdev) == 1 && minor(sb.st_rdev) == 3);
    close(fd);
    fd = open("/dev/zero", O_RDONLY);
    memset(b, 0xff, sizeof b);
    CHECK(fd >= 0 && read(fd, b, 16) == 16 && b[0] == 0 && b[15] == 0);
    close(fd);
    fd = open("/dev/urandom", O_RDONLY);
    char r1[32], r2[32];
    CHECK(fd >= 0 && read(fd, r1, 32) == 32 && read(fd, r2, 32) == 32 && memcmp(r1, r2, 32) != 0);
    close(fd);
    fd = open("/dev/tty", O_RDWR);
    CHECK(fd >= 0 && isatty(fd) && tcgetpgrp(fd) == tcgetpgrp(0));
    close(fd);
    CHECK(stat("/dev/console", &sb) == 0 && S_ISCHR(sb.st_mode));
    errno = 0; CHECK(open("/dev/nosuch", O_RDONLY) == -1 && errno == ENOENT);

    tcsetattr(0, TCSANOW, &orig);
    printf("tty: done, %d failed\n", bad);
    return bad;
}
