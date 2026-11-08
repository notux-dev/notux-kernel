#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("io: %-58s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

static int put(const char *path, const char *data)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int n = write(fd, data, strlen(data));
    close(fd);
    return n;
}
static int slurp(const char *path, char *buf, int max)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, max - 1);
    close(fd);
    if (n >= 0) buf[n] = 0;
    return n;
}
static long bsz = 512;
static long bfree(void)
{
    struct statfs s;
    return statfs("/", &s) == 0 ? (long)s.f_bfree * ((long)s.f_bsize / 512) : -1;
}

int main(void)
{
    char buf[256];
    struct stat st;

    struct statfs sf0;
    int x6 = statfs("/", &sf0) == 0 && sf0.f_namelen == 14;
    if (!x6) bsz = sf0.f_bsize;
    printf("io: root filesystem is %s\n", x6 ? "xv6fs" : "ext2");
    CHECK(chdir("/etc") == 0);

    CHECK(mkdir("d1", 0755) == 0);
    CHECK(stat("d1", &st) == 0 && S_ISDIR(st.st_mode));
    errno = 0; CHECK(mkdir("d1", 0755) == -1 && errno == EEXIST);
    errno = 0; CHECK(mkdir("nodir/x", 0755) == -1 && errno == ENOENT);
    if (x6) {
        errno = 0; CHECK(mkdir("abcdefghijklmnop", 0755) == -1 && errno == ENAMETOOLONG);
        errno = 0; CHECK(open("abcdefghijklmnopq", O_RDONLY) == -1 && errno == ENOENT);
        errno = 0; CHECK(open("abcdefghijklmnop", O_CREAT | O_WRONLY, 0644) == -1 && errno == ENAMETOOLONG);
    } else {
        char longn[300]; memset(longn, 'n', sizeof longn - 1); longn[sizeof longn - 1] = 0;
        errno = 0; CHECK(mkdir(longn, 0755) == -1 && errno == ENAMETOOLONG);
        errno = 0; CHECK(open("abcdefghijklmnopq", O_RDONLY) == -1 && errno == ENOENT);
        CHECK(mkdir("abcdefghijklmnop", 0755) == 0 && rmdir("abcdefghijklmnop") == 0);
    }
    int dfd = open("d1", O_RDONLY | O_DIRECTORY);
    CHECK(dfd >= 0);
    CHECK(mkdirat(dfd, "sub", 0755) == 0 && stat("d1/sub", &st) == 0);
    CHECK(rmdir("d1/sub") == 0);
    errno = 0; CHECK(rmdir("d1/sub") == -1 && errno == ENOENT);

    CHECK(put("d1/f", "hello") == 5);
    errno = 0; CHECK(rmdir("d1/f") == -1 && errno == ENOTDIR);
    errno = 0; CHECK(unlink("d1") == -1 && errno == EISDIR);
    errno = 0; CHECK(rmdir("d1") == -1 && errno == ENOTEMPTY);
    errno = 0; CHECK(rmdir(".") == -1 && errno == EINVAL);

    CHECK(rename("d1/f", "d1/g") == 0);
    errno = 0; CHECK(stat("d1/f", &st) == -1 && errno == ENOENT);
    CHECK(slurp("d1/g", buf, sizeof buf) == 5 && !strcmp(buf, "hello"));
    CHECK(mkdir("d2", 0755) == 0);
    CHECK(rename("d1/g", "d2/h") == 0 && slurp("d2/h", buf, sizeof buf) == 5);
    CHECK(put("d2/old", "OLD") == 3);
    CHECK(rename("d2/h", "d2/old") == 0);
    CHECK(slurp("d2/old", buf, sizeof buf) == 5 && !strcmp(buf, "hello"));
    errno = 0; CHECK(rename("d2/none", "d2/x") == -1 && errno == ENOENT);
    CHECK(rename("d2", "d3") == 0 && stat("d3/old", &st) == 0);
    if (x6) { errno = 0; CHECK(rename("d3", "d1/d3") == -1 && errno == EXDEV); }
    else CHECK(rename("d3", "d1/d3") == 0 && rename("d1/d3", "d3") == 0);
    CHECK(mkdir("d1/inner", 0755) == 0);
    CHECK(put("d3/zz", "z") == 1);
    errno = 0; CHECK(rename("d3/zz", "d1") == -1 && errno == EISDIR);
    errno = 0; CHECK(rename("d1/inner", "d3/zz") == -1 && errno == ENOTDIR);
    CHECK(renameat(dfd, "inner", dfd, "inner2") == 0 && stat("d1/inner2", &st) == 0);
    CHECK(rmdir("d1/inner2") == 0);

    CHECK(link("d3/old", "d3/lnk") == 0);
    CHECK(stat("d3/old", &st) == 0 && st.st_nlink == 2);
    errno = 0; CHECK(link("d3/old", "d3/lnk") == -1 && errno == EEXIST);
    errno = 0; CHECK(link("d3", "d1/dl") == -1 && errno == EPERM);
    CHECK(unlink("d3/old") == 0);
    CHECK(slurp("d3/lnk", buf, sizeof buf) == 5 && !strcmp(buf, "hello"));
    CHECK(stat("d3/lnk", &st) == 0 && st.st_nlink == 1);
    if (x6) { errno = 0; CHECK(symlink("x", "d3/sl") == -1 && errno == EPERM); }
    else CHECK(symlink("x", "d3/sl") == 0 && unlink("d3/sl") == 0);

    char big[4096];
    memset(big, 'A', sizeof big);
    long b0 = bfree();
    int fd = open("tf", O_RDWR | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    for (int i = 0; i < 10; i++) write(fd, big, sizeof big);
    long b1 = bfree();
    CHECK(b0 - b1 >= 80);
    CHECK(fstat(fd, &st) == 0 && st.st_size == 40960);
    CHECK(ftruncate(fd, 1000) == 0 && fstat(fd, &st) == 0 && st.st_size == 1000);
    long b2 = bfree();
    CHECK(b2 - b1 >= 70);
    CHECK(pread(fd, buf, 4, 998) == 2);
    CHECK(ftruncate(fd, 3000) == 0 && pread(fd, buf, 1, 2999) == 1 && buf[0] == 0);
    CHECK(pread(fd, buf, 1, 999) == 1 && buf[0] == 'A');
    CHECK(pread(fd, buf, 1, 1000) == 1 && buf[0] == 0);
    CHECK(pwrite(fd, "XYZ", 3, 500) == 3);
    CHECK(lseek(fd, 0, SEEK_CUR) == 40960);
    CHECK(pread(fd, buf, 3, 500) == 3 && !memcmp(buf, "XYZ", 3));
    CHECK(truncate("tf", 10) == 0 && stat("tf", &st) == 0 && st.st_size == 10);
    CHECK(truncate("tf", 0) == 0 && stat("tf", &st) == 0 && st.st_size == 0);
    CHECK(bfree() >= b0 - 16);
    errno = 0; CHECK(ftruncate(fd, -1) == -1 && errno == EINVAL);
    close(fd);
    int ro = open("tf", O_RDONLY);
    errno = 0; CHECK(ftruncate(ro, 0) == -1 && errno == EINVAL);
    close(ro);

    fd = open("tf2", O_WRONLY | O_CREAT, 0644);
    for (int i = 0; i < 8; i++) write(fd, big, sizeof big);
    close(fd);
    b1 = bfree();
    fd = open("tf2", O_WRONLY | O_TRUNC);
    close(fd);
    CHECK(bfree() - b1 >= 60);
    CHECK(unlink("tf2") == 0 && unlink("tf") == 0);

    struct statfs sf;
    CHECK(statfs("/", &sf) == 0 && sf.f_bsize == (x6 ? 512 : bsz) && sf.f_blocks > 1000 &&
          sf.f_bfree > 0 && sf.f_bfree < sf.f_blocks && sf.f_files > 50 && sf.f_ffree > 0 &&
          sf.f_namelen == (x6 ? 14 : 255));
    CHECK(fstatfs(dfd, &sf) == 0);
    errno = 0; CHECK(statfs("/nonexistent", &sf) == -1 && errno == ENOENT);

    CHECK(fchdir(dfd) == 0);
    char cwd[64];
    CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/etc/d1"));
    CHECK(chdir("/etc") == 0);
    fd = open("d3/lnk", O_RDWR);
    CHECK(fsync(fd) == 0 && fdatasync(fd) == 0);
    errno = 0; CHECK(fchdir(fd) == -1 && errno == ENOTDIR);
    CHECK(fchmod(fd, 0600) == 0 && fchown(fd, 0, 0) == 0);
    CHECK(chmod("d3/lnk", 0600) == 0);
    errno = 0; CHECK(chmod("d3/none", 0600) == -1 && errno == ENOENT);
    CHECK(chown("d3/lnk", 0, 0) == 0);
    CHECK(utimensat(AT_FDCWD, "d3/lnk", 0, 0) == 0);
    CHECK(fchmodat(AT_FDCWD, "d3/lnk", 0600, 0) == 0);
    sync();
    close(fd);
    errno = 0; CHECK(fsync(99) == -1 && errno == EBADF);

    int pf[2], n = -1;
    CHECK(pipe(pf) == 0);
    write(pf[1], "12345", 5);
    CHECK(ioctl(pf[0], FIONREAD, &n) == 0 && n == 5);
    int one = 1;
    CHECK(ioctl(pf[0], FIONBIO, &one) == 0 && (fcntl(pf[0], F_GETFL) & O_NONBLOCK));
    CHECK(ioctl(pf[0], FIOCLEX) == 0 && fcntl(pf[0], F_GETFD) == FD_CLOEXEC);
    close(pf[0]); close(pf[1]);
    fd = open("d3/lnk", O_RDONLY);
    CHECK(ioctl(fd, FIONREAD, &n) == 0 && n == 5);
    errno = 0; CHECK(ioctl(fd, 0x5401, &n) == -1 && errno == ENOTTY);
    close(fd);

    errno = 0; CHECK(syscall(9999) == -1 && errno == ENOSYS);

    CHECK(unlink("d3/lnk") == 0 && unlink("d3/zz") == 0);
    CHECK(unlinkat(AT_FDCWD, "d3", AT_REMOVEDIR) == 0);
    close(dfd);
    CHECK(unlinkat(AT_FDCWD, "d1", AT_REMOVEDIR) == 0);
    errno = 0; CHECK(stat("d1", &st) == -1 && errno == ENOENT);

    printf("io: failures=%d\n", bad);
    return bad;
}
