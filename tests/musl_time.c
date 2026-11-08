#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("time: %-52s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main(void)
{
    struct timespec a, b, res;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &a) == 0);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &b) == 0 &&
          (b.tv_sec > a.tv_sec || b.tv_nsec >= a.tv_nsec));
    CHECK(clock_getres(CLOCK_MONOTONIC, &res) == 0 && res.tv_sec == 0 && res.tv_nsec > 0);
    errno = 0; CHECK(clock_gettime(99, &a) == -1 && errno == EINVAL);

    struct timeval tv;
    time_t t1 = time(0);
    CHECK(gettimeofday(&tv, 0) == 0 && tv.tv_sec >= t1 - 1 && tv.tv_sec <= t1 + 1);
    CHECK(t1 > 1700000000);
    struct timespec rt;
    CHECK(clock_gettime(CLOCK_REALTIME, &rt) == 0 && rt.tv_sec >= t1 - 1);

    long t0 = now_ms();
    struct timespec req = {0, 200 * 1000 * 1000};
    CHECK(nanosleep(&req, 0) == 0);
    long el = now_ms() - t0;
    printf("time: slept %ld ms\n", el);
    CHECK(el >= 190 && el < 400);
    t0 = now_ms();
    usleep(50000);
    el = now_ms() - t0;
    CHECK(el >= 40 && el < 200);
    req.tv_sec = 0; req.tv_nsec = 2000000000L;
    errno = 0; CHECK(nanosleep(&req, 0) == -1 && errno == EINVAL);
    t0 = now_ms();
    sleep(1);
    el = now_ms() - t0;
    CHECK(el >= 990 && el < 1300);

    int pf[2];
    CHECK(pipe(pf) == 0);
    struct pollfd p[2] = {{pf[0], POLLIN, 0}, {pf[1], POLLOUT, 0}};
    CHECK(poll(p, 2, 0) == 1 && !p[0].revents && (p[1].revents & POLLOUT));
    t0 = now_ms();
    p[1].fd = -1;
    CHECK(poll(p, 2, 150) == 0);
    el = now_ms() - t0;
    CHECK(el >= 140 && el < 400);

    pid_t pid = fork();
    if (pid == 0)
    {
        close(pf[0]);
        usleep(200000);
        write(pf[1], "x", 1);
        usleep(200000);
        close(pf[1]);
        _exit(0);
    }
    t0 = now_ms();
    p[1].fd = pf[1];
    p[1].events = 0;
    CHECK(poll(p, 1, -1) == 1 && (p[0].revents & POLLIN));
    el = now_ms() - t0;
    printf("time: poll woke after %ld ms\n", el);
    CHECK(el >= 150 && el < 600);
    char c;
    CHECK(read(pf[0], &c, 1) == 1);
    close(pf[1]);
    t0 = now_ms();
    p[0].revents = 0;
    CHECK(poll(p, 1, 2000) == 1 && (p[0].revents & POLLHUP));
    el = now_ms() - t0;
    CHECK(el >= 100 && el < 1000);
    int st;
    waitpid(pid, &st, 0);
    close(pf[0]);

    CHECK(pipe(pf) == 0);
    fd_set rs;
    struct timeval to = {0, 100000};
    FD_ZERO(&rs); FD_SET(pf[0], &rs);
    t0 = now_ms();
    CHECK(select(pf[0] + 1, &rs, 0, 0, &to) == 0 && !FD_ISSET(pf[0], &rs));
    el = now_ms() - t0;
    CHECK(el >= 90 && el < 400);
    write(pf[1], "y", 1);
    FD_ZERO(&rs); FD_SET(pf[0], &rs);
    to.tv_sec = 1; to.tv_usec = 0;
    CHECK(select(pf[0] + 1, &rs, 0, 0, &to) == 1 && FD_ISSET(pf[0], &rs));
    fd_set ws;
    FD_ZERO(&ws); FD_SET(pf[1], &ws);
    CHECK(select(pf[1] + 1, 0, &ws, 0, 0) == 1 && FD_ISSET(pf[1], &ws));

    struct pollfd pp = {pf[0], POLLIN, 0};
    struct timespec pts = {0, 50000000};
    CHECK(ppoll(&pp, 1, &pts, 0) == 1 && (pp.revents & POLLIN));
    read(pf[0], &c, 1);
    CHECK(ppoll(&pp, 1, &pts, 0) == 0);

    struct pollfd bad_fd = {50, POLLIN, 0};
    CHECK(poll(&bad_fd, 1, 0) == 1 && (bad_fd.revents & POLLNVAL));
    errno = 0;
    FD_ZERO(&rs); FD_SET(50, &rs);
    CHECK(select(51, &rs, 0, 0, &to) == -1 && errno == EBADF);

    printf("time: failures=%d\n", bad);
    return bad;
}
