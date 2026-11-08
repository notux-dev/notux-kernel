#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("fd: %-44s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child"))
    {
        int closed = atoi(argv[2]), kept = atoi(argv[3]);
        errno = 0;
        int a = fcntl(closed, F_GETFD);
        int b = fcntl(kept, F_GETFD);
        printf("fd: child sees cloexec-fd gone=%d kept-fd open=%d\n", a == -1 && errno == EBADF, b >= 0);
        return (a == -1 && errno == EBADF && b >= 0) ? 0 : 1;
    }

    char b1[8], b2[8], b3[8], ref[32];
    int fd = open("/README", O_RDONLY);
    CHECK(fd >= 3);
    CHECK(pread(fd, ref, 20, 0) == 20);

    int d = dup(fd);
    CHECK(d > fd);
    CHECK(read(fd, b1, 5) == 5);
    CHECK(read(d, b2, 5) == 5);
    CHECK(!memcmp(b1, ref, 5) && !memcmp(b2, ref + 5, 5));
    CHECK(lseek(d, 2, SEEK_SET) == 2);
    CHECK(lseek(fd, 0, SEEK_CUR) == 2);
    CHECK(read(fd, b3, 3) == 3 && !memcmp(b3, ref + 2, 3));

    lseek(fd, 0, SEEK_SET);
    pid_t pid = fork();
    if (pid == 0) { char c[6]; _exit(read(fd, c, 5) == 5 ? 0 : 1); }
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    CHECK(lseek(fd, 0, SEEK_CUR) == 5);
    CHECK(read(fd, b1, 5) == 5 && !memcmp(b1, ref + 5, 5));

    int w = open("/etc/fdtest", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(w >= 0);
    int w2 = dup(w);
    CHECK(write(w, "AAA", 3) == 3);
    CHECK(write(w2, "BBB", 3) == 3);
    close(w); close(w2);
    char buf[16] = {0};
    int r = open("/etc/fdtest", O_RDONLY);
    CHECK(read(r, buf, 15) == 6 && !strcmp(buf, "AAABBB"));
    close(r);

    int ap = open("/etc/fdtest", O_WRONLY | O_APPEND);
    CHECK(ap >= 0 && (fcntl(ap, F_GETFL) & O_APPEND));
    lseek(ap, 0, SEEK_SET);
    CHECK(write(ap, "C", 1) == 1);
    close(ap);
    r = open("/etc/fdtest", O_RDONLY);
    memset(buf, 0, sizeof buf);
    CHECK(read(r, buf, 15) == 7 && !strcmp(buf, "AAABBBC"));
    close(r);

    errno = 0; CHECK(read(99, buf, 1) == -1 && errno == EBADF);
    errno = 0; CHECK(open("/nonexistent", O_RDONLY) == -1 && errno == ENOENT);
    errno = 0; CHECK(open("/etc/fdtest", O_CREAT | O_EXCL | O_WRONLY, 0644) == -1 && errno == EEXIST);
    errno = 0; CHECK(open("/README", O_RDONLY | O_DIRECTORY) == -1 && errno == ENOTDIR);
    errno = 0; CHECK(open("/bin", O_WRONLY) == -1 && errno == EISDIR);
    errno = 0; CHECK(write(fd, "x", 1) == -1 && errno == EBADF);

    int hi = fcntl(fd, F_DUPFD, 10);
    CHECK(hi == 10);
    CHECK(fcntl(hi, F_GETFD) == 0);
    CHECK(fcntl(hi, F_SETFD, FD_CLOEXEC) == 0 && fcntl(hi, F_GETFD) == FD_CLOEXEC);
    int e3 = dup3(fd, 20, O_CLOEXEC);
    CHECK(e3 == 20 && fcntl(20, F_GETFD) == FD_CLOEXEC);
    errno = 0; CHECK(dup3(fd, fd, 0) == -1 && errno == EINVAL);
    CHECK(dup2(fd, 30) == 30 && fcntl(30, F_GETFD) == 0);

    pid = fork();
    if (pid == 0)
    {
        char a1[8], a2[8];
        snprintf(a1, sizeof a1, "%d", 10);
        snprintf(a2, sizeof a2, "%d", 30);
        char *av[] = {"musl_fd", "child", a1, a2, 0};
        char *ev[] = {0};
        execve("/bin/musl_fd", av, ev);
        _exit(99);
    }
    waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);

    int oc = open("/README", O_RDONLY | O_CLOEXEC);
    CHECK(oc >= 0 && fcntl(oc, F_GETFD) == FD_CLOEXEC);

    int n = 0, last;
    while ((last = open("/README", O_RDONLY)) >= 0) n++;
    CHECK(last == -1 && errno == EMFILE && n > 20);
    for (int i = 3; i < 64; i++) close(i);
    CHECK(open("/README", O_RDONLY) >= 0);

    {
        int pf[2];
        CHECK(pipe(pf) == 0 && pf[0] >= 3 && pf[1] > pf[0]);
        CHECK(write(pf[1], "hello", 5) == 5);
        char pb[16] = {0};
        CHECK(read(pf[0], pb, 16) == 5 && !strcmp(pb, "hello"));
        errno = 0; CHECK(write(pf[0], "x", 1) == -1 && errno == EBADF);
        errno = 0; CHECK(read(pf[1], pb, 1) == -1 && errno == EBADF);
        errno = 0; CHECK(lseek(pf[0], 0, SEEK_SET) == -1 && errno == ESPIPE);
        struct stat ps;
        CHECK(fstat(pf[0], &ps) == 0 && S_ISFIFO(ps.st_mode));

        close(pf[1]);
        CHECK(read(pf[0], pb, 16) == 0);
        close(pf[0]);

        CHECK(pipe(pf) == 0);
        close(pf[0]);
        errno = 0; CHECK(write(pf[1], "x", 1) == -1 && errno == EPIPE);
        close(pf[1]);

        CHECK(pipe2(pf, O_NONBLOCK | O_CLOEXEC) == 0);
        errno = 0; CHECK(read(pf[0], pb, 1) == -1 && errno == EAGAIN);
        CHECK(fcntl(pf[0], F_GETFD) == FD_CLOEXEC);
        close(pf[0]); close(pf[1]);

        CHECK(pipe(pf) == 0);
        pid = fork();
        if (pid == 0)
        {
            close(pf[0]);
            for (volatile long i = 0; i < 20000000; i++) ;
            write(pf[1], "late", 4);
            _exit(0);
        }
        close(pf[1]);
        memset(pb, 0, sizeof pb);
        CHECK(read(pf[0], pb, 16) == 4 && !strcmp(pb, "late"));
        CHECK(read(pf[0], pb, 16) == 0);
        waitpid(pid, &st, 0);
        close(pf[0]);

        CHECK(pipe(pf) == 0);
        pid = fork();
        if (pid == 0)
        {
            close(pf[0]);
            static char chunk[1000];
            long sent = 0;
            for (int i = 0; i < 100; i++)
            {
                memset(chunk, 'a' + i % 26, sizeof chunk);
                long off = 0;
                while (off < (long)sizeof chunk)
                {
                    long w = write(pf[1], chunk + off, sizeof chunk - off);
                    if (w <= 0) _exit(1);
                    off += w;
                }
                sent += off;
            }
            _exit(sent == 100000 ? 0 : 2);
        }
        close(pf[1]);
        {
            static char rb[3000];
            long got = 0, ok = 1;
            for (;;)
            {
                long n2 = read(pf[0], rb, sizeof rb);
                if (n2 <= 0) break;
                for (long i = 0; i < n2; i++)
                    if (rb[i] != 'a' + ((got + i) / 1000) % 26) ok = 0;
                got += n2;
            }
            CHECK(got == 100000 && ok);
        }
        waitpid(pid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
        close(pf[0]);

        CHECK(pipe(pf) == 0);
        pid = fork();
        if (pid == 0)
        {
            dup2(pf[1], 1);
            close(pf[0]); close(pf[1]);
            char *av[] = {"musl_hello", 0};
            char *ev[] = {0};
            execve("/bin/musl_hello", av, ev);
            _exit(99);
        }
        close(pf[1]);
        {
            static char all[512];
            long got = 0, n2;
            while ((n2 = read(pf[0], all + got, sizeof all - 1 - got)) > 0) got += n2;
            all[got] = 0;
            CHECK(strstr(all, "malloc+memset ok") != 0);
        }
        waitpid(pid, &st, 0);
        close(pf[0]);
    }

    printf("fd: failures=%d\n", bad);
    return bad;
}
