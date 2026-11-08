#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("proc: %-56s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

static int slurp(const char *path, char *buf, int cap)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = 0, r;
    while (n < cap - 1 && (r = read(fd, buf + n, cap - 1 - n)) > 0) n += r;
    buf[n] = 0;
    close(fd);
    return n;
}

int main(void)
{
    static char b[4096];
    int n;

    n = slurp("/proc/uptime", b, sizeof b);
    CHECK(n > 3 && strchr(b, '.') && atof(b) >= 0);

    n = slurp("/proc/meminfo", b, sizeof b);
    CHECK(n > 0 && strstr(b, "MemTotal:") && strstr(b, "MemFree:") && strstr(b, " kB\n"));
    long total = 0, fr = 0;
    char *p = strstr(b, "MemTotal:"); if (p) total = atol(p + 9);
    p = strstr(b, "MemFree:"); if (p) fr = atol(p + 8);
    CHECK(total > 1000 && fr > 0 && fr <= total);

    n = slurp("/proc/cpuinfo", b, sizeof b);
    CHECK(n > 0 && strstr(b, "processor") && strstr(b, "vendor_id") && strstr(b, "model name") && strstr(b, "flags"));

    n = slurp("/proc/version", b, sizeof b);
    CHECK(n > 0 && strstr(b, "version"));
    n = slurp("/proc/mounts", b, sizeof b);
    CHECK(n > 0 && strstr(b, " / "));
    n = slurp("/proc/loadavg", b, sizeof b);
    CHECK(n > 0);
    n = slurp("/proc/stat", b, sizeof b);
    CHECK(n > 0 && strstr(b, "cpu ") && strstr(b, "btime"));

    n = slurp("/proc/self/status", b, sizeof b);
    char want[32]; snprintf(want, sizeof want, "Pid:\t%d\n", (int)getpid());
    CHECK(n > 0 && strstr(b, "Name:\tmusl_proc") && strstr(b, want));
    snprintf(want, sizeof want, "/proc/%d/status", (int)getpid());
    n = slurp(want, b, sizeof b);
    CHECK(n > 0 && strstr(b, "State:\tR"));
    n = slurp("/proc/self/stat", b, sizeof b);
    int pid = 0; char comm[32] = "";
    sscanf(b, "%d (%31[^)])", &pid, comm);
    CHECK(pid == getpid() && !strcmp(comm, "musl_proc"));
    n = slurp("/proc/self/cmdline", b, sizeof b);
    CHECK(n == 0);

    struct stat st;
    CHECK(stat("/proc", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(stat("/proc/cpuinfo", &st) == 0 && S_ISREG(st.st_mode));
    CHECK(stat("/proc/self", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(access("/proc/meminfo", R_OK) == 0);
    errno = 0; CHECK(stat("/proc/nosuch", &st) == -1 && errno == ENOENT);
    errno = 0; CHECK(open("/proc/nosuch", O_RDONLY) == -1 && errno == ENOENT);
    errno = 0; CHECK(open("/proc/uptime", O_WRONLY) == -1 && errno == EACCES);
    errno = 0; CHECK(open("/proc/new", O_CREAT | O_WRONLY, 0644) == -1 && errno == EACCES);
    errno = 0; CHECK(open("/proc/uptime", O_RDONLY | O_DIRECTORY) == -1 && errno == ENOTDIR);
    errno = 0; CHECK(open("/proc", O_RDWR) == -1 && errno == EISDIR);

    int fd = open("/proc/version", O_RDONLY);
    CHECK(fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
    char x[8], y[8];
    CHECK(read(fd, x, 6) == 6 && lseek(fd, 0, SEEK_SET) == 0 && read(fd, y, 6) == 6 && !memcmp(x, y, 6));
    close(fd);

    DIR *d = opendir("/proc");
    int saw_self = 0, saw_mem = 0, saw_pid = 0, cnt = 0;
    char pidname[16]; snprintf(pidname, sizeof pidname, "%d", (int)getpid());
    struct dirent *de;
    while (d && (de = readdir(d)))
    {
        cnt++;
        if (!strcmp(de->d_name, "self")) saw_self = 1;
        if (!strcmp(de->d_name, "meminfo")) saw_mem = 1;
        if (!strcmp(de->d_name, pidname)) saw_pid = 1;
    }
    CHECK(d && cnt >= 10 && saw_self && saw_mem && saw_pid);
    if (d) closedir(d);
    d = opendir("/proc/self");
    int saw_status = 0;
    while (d && (de = readdir(d))) if (!strcmp(de->d_name, "status")) saw_status = 1;
    CHECK(d && saw_status);
    if (d) closedir(d);

    int pp[2]; pipe(pp);
    pid_t k = fork();
    if (k == 0) { char c; close(pp[1]); read(pp[0], &c, 1); _exit(0); }
    close(pp[0]);
    usleep(100000);
    snprintf(want, sizeof want, "/proc/%d/status", (int)k);
    n = slurp(want, b, sizeof b);
    snprintf(comm, sizeof comm, "PPid:\t%d\n", (int)getpid());
    CHECK(n > 0 && strstr(b, "State:\tS") && strstr(b, comm));
    write(pp[1], "x", 1);
    int stt; waitpid(k, &stt, 0);
    errno = 0; CHECK(open(want, O_RDONLY) == -1 && errno == ENOENT);

    printf("proc: done, %d failed\n", bad);
    return bad;
}
