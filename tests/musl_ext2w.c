#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/time.h>

static int bad;
#define BIGN (700L * 1024 + 777)
#define CHECK(c) do { int ok_ = !!(c); printf("ext2w: %-58s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

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
static int put(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int r = write(fd, s, strlen(s));
    close(fd);
    return r;
}
static unsigned char pat(long k) { return (unsigned char)((k * 31 + (k >> 9)) & 0xff); }

int main(void)
{
    static char b[8192];
    struct stat st;
    if (stat("/mnt/hello.txt", &st) != 0) { printf("ext2w: no ext2 disk, skipped\n"); return 0; }

    CHECK(mkdir("/mnt/w", 0755) == 0 && stat("/mnt/w", &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0755);
    CHECK(mkdir("/mnt/w", 0755) == -1 && errno == EEXIST);
    CHECK(chdir("/mnt/w") == 0);

    CHECK(put("small.txt", "hello write\n") == 12);
    CHECK(slurp("small.txt", b, sizeof b) == 12 && !strcmp(b, "hello write\n"));
    CHECK(stat("small.txt", &st) == 0 && st.st_size == 12 && (st.st_mode & 0777) == 0644 && st.st_nlink == 1);
    CHECK(put("small.txt", "x") == 1 && slurp("small.txt", b, sizeof b) == 1);

    {
        int fd = open("app.txt", O_WRONLY | O_CREAT | O_APPEND, 0600);
        CHECK(fd >= 0 && write(fd, "aaa", 3) == 3 && write(fd, "bbb", 3) == 3);
        close(fd);
        CHECK(slurp("app.txt", b, sizeof b) == 6 && !strcmp(b, "aaabbb"));
        fd = open("hole.bin", O_RDWR | O_CREAT, 0644);
        CHECK(pwrite(fd, "END", 3, 100000) == 3);
        CHECK(fstat(fd, &st) == 0 && st.st_size == 100003);
        char z[16]; memset(z, 1, 16);
        CHECK(pread(fd, z, 16, 50000) == 16 && z[0] == 0 && z[15] == 0);
        CHECK(pread(fd, b, 3, 100000) == 3 && !memcmp(b, "END", 3));
        close(fd);
    }

    {
        int fd = open("big.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        long total = 0; static unsigned char buf[4096]; int ok = fd >= 0;
        while (total < BIGN && ok) {
            int n = sizeof buf; if (total + n > BIGN) n = BIGN - total;
            for (int i = 0; i < n; i++) buf[i] = pat(total + i);
            if (write(fd, buf, n) != n) ok = 0;
            total += n;
        }
        close(fd);
        CHECK(ok && stat("big.bin", &st) == 0 && st.st_size == BIGN);
        fd = open("big.bin", O_RDONLY);
        long t = 0, badb = 0; int r;
        while ((r = read(fd, buf, sizeof buf)) > 0) { for (int i = 0; i < r; i++) if (buf[i] != pat(t + i)) badb++; t += r; }
        close(fd);
        CHECK(t == BIGN && badb == 0);
        CHECK(truncate("big.bin", 100000) == 0 && stat("big.bin", &st) == 0 && st.st_size == 100000);
        fd = open("big.bin", O_RDONLY); badb = 0; t = 0;
        while ((r = read(fd, buf, sizeof buf)) > 0) { for (int i = 0; i < r; i++) if (buf[i] != pat(t + i)) badb++; t += r; }
        close(fd);
        CHECK(t == 100000 && badb == 0);
        CHECK(truncate("big.bin", 5000) == 0 && truncate("big.bin", 20000) == 0);
        fd = open("big.bin", O_RDONLY);
        CHECK(pread(fd, buf, 100, 5000) == 100 && buf[0] == 0 && buf[99] == 0 && pread(fd, buf, 4, 4990) == 4 && buf[0] == pat(4990));
        close(fd);
        CHECK(truncate("big.bin", 0) == 0 && stat("big.bin", &st) == 0 && st.st_size == 0);
    }

    CHECK(mkdir("d1", 0755) == 0 && mkdir("d1/d2", 0755) == 0 && put("d1/d2/f", "deep") == 4);
    CHECK(stat("d1", &st) == 0 && st.st_nlink == 3);
    CHECK(rmdir("d1") == -1 && errno == ENOTEMPTY);
    CHECK(unlink("d1") == -1 && errno == EISDIR);
    CHECK(rmdir("small.txt") == -1 && errno == ENOTDIR);
    CHECK(unlink("d1/d2/f") == 0 && rmdir("d1/d2") == 0 && stat("d1", &st) == 0 && st.st_nlink == 2);
    CHECK(rmdir("d1") == 0 && stat("d1", &st) == -1 && errno == ENOENT);

    CHECK(put("r1", "one") == 3 && rename("r1", "r2") == 0 && stat("r1", &st) == -1 && slurp("r2", b, sizeof b) == 3);
    CHECK(put("r3", "three") == 5 && rename("r3", "r2") == 0 && slurp("r2", b, sizeof b) == 5 && !strcmp(b, "three"));
    CHECK(mkdir("ra", 0755) == 0 && mkdir("rb", 0755) == 0 && mkdir("ra/sub", 0755) == 0);
    CHECK(rename("ra/sub", "rb/moved") == 0);
    CHECK(stat("ra", &st) == 0 && st.st_nlink == 2 && stat("rb", &st) == 0 && st.st_nlink == 3);
    CHECK(chdir("rb/moved") == 0 && chdir("..") == 0 && chdir("..") == 0);
    {
        char cwd[256];
        CHECK(chdir("/mnt/w/rb/moved") == 0 && chdir("..") == 0 && getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/mnt/w/rb"));
        chdir("/mnt/w");
    }
    CHECK(rename("rb", "rb/moved/inside") == -1 && errno == EINVAL);
    struct stat sx_e, sx_m;
    int xdev = stat("/etc", &sx_e) == 0 && stat("/mnt", &sx_m) == 0 && sx_e.st_dev != sx_m.st_dev;
    if (xdev) CHECK(rename("small.txt", "/etc/small_xdev") == -1 && errno == EXDEV);
    CHECK(rename("nonexistent", "zzz") == -1 && errno == ENOENT);
    CHECK(rmdir("rb/moved") == 0 && rmdir("rb") == 0 && rmdir("ra") == 0);

    {
        CHECK(put("h1", "linked") == 6 && link("h1", "h2") == 0 && stat("h1", &st) == 0 && st.st_nlink == 2);
        CHECK(unlink("h1") == 0 && stat("h2", &st) == 0 && st.st_nlink == 1 && slurp("h2", b, sizeof b) == 6);
        int fd = open("h2", O_RDONLY);
        CHECK(unlink("h2") == 0 && stat("h2", &st) == -1);
        CHECK(fd >= 0 && read(fd, b, 6) == 6 && !strncmp(b, "linked", 6));
        close(fd);
        if (xdev) CHECK(link("/etc/passwd", "xfs_link") == -1 && errno == EXDEV);
    }

    {
        char t[300];
        CHECK(symlink("small.txt", "sl1") == 0 && readlink("sl1", t, sizeof t) == 9 && !strncmp(t, "small.txt", 9));
        CHECK(slurp("sl1", b, sizeof b) == 1 && lstat("sl1", &st) == 0 && S_ISLNK(st.st_mode) && st.st_size == 9);
        char longt[200]; memset(longt, 'x', 150); longt[150] = 0; memcpy(longt, "/mnt/w/", 7);
        CHECK(symlink(longt, "sl2") == 0 && readlink("sl2", t, sizeof t) == 150 && !strncmp(t, "/mnt/w/xxxx", 11));
        CHECK(symlink("x", "sl1") == -1 && errno == EEXIST);
        CHECK(unlink("sl1") == 0 && unlink("sl2") == 0);
    }

    {
        CHECK(chmod("small.txt", 0600) == 0 && stat("small.txt", &st) == 0 && (st.st_mode & 07777) == 0600 && S_ISREG(st.st_mode));
        CHECK(chown("small.txt", 1000, 100) == 0 && stat("small.txt", &st) == 0 && st.st_uid == 1000 && st.st_gid == 100);
        struct timespec ts[2] = {{1000000000, 0}, {1234567890, 0}};
        CHECK(utimensat(AT_FDCWD, "small.txt", ts, 0) == 0 && stat("small.txt", &st) == 0 && st.st_mtime == 1234567890 && st.st_atime == 1000000000);
        int fd = open("small.txt", O_RDONLY);
        CHECK(fchmod(fd, 0640) == 0 && fstat(fd, &st) == 0 && (st.st_mode & 07777) == 0640);
        close(fd);
    }

    {
        CHECK(mkdir("many", 0755) == 0);
        char nm[300]; int made = 0;
        for (int i = 0; i < 150; i++) { snprintf(nm, sizeof nm, "many/entry_with_a_long_name_%03d.dat", i); if (put(nm, "z") == 1) made++; }
        CHECK(made == 150);
        DIR *d = opendir("many"); int n = 0; struct dirent *e;
        while (d && (e = readdir(d))) if (strncmp(e->d_name, "entry_", 6) == 0) n++;
        if (d) closedir(d);
        CHECK(n == 150);
        int gone = 0;
        for (int i = 0; i < 150; i += 2) { snprintf(nm, sizeof nm, "many/entry_with_a_long_name_%03d.dat", i); if (unlink(nm) == 0) gone++; }
        CHECK(gone == 75 && stat("many/entry_with_a_long_name_001.dat", &st) == 0 && stat("many/entry_with_a_long_name_002.dat", &st) == -1);
        CHECK(put("many/new_one.dat", "n") == 1);
        for (int i = 1; i < 150; i += 2) { snprintf(nm, sizeof nm, "many/entry_with_a_long_name_%03d.dat", i); unlink(nm); }
        CHECK(unlink("many/new_one.dat") == 0 && rmdir("many") == 0);
    }

    {
        char nm[300]; memset(nm, 'n', 255); nm[255] = 0;
        CHECK(put(nm, "ok") == 2 && slurp(nm, b, sizeof b) == 2 && unlink(nm) == 0);
        memset(nm, 'n', 256); nm[256] = 0;
        CHECK(open(nm, O_WRONLY | O_CREAT, 0644) == -1 && errno == ENAMETOOLONG);
    }

    CHECK(put("/etc/ext2w_probe", "ok") == 2 && unlink("/etc/ext2w_probe") == 0);

    CHECK(put("keep.txt", "kept by ext2w\n") == 14 && mkdir("keepdir", 0750) == 0 && put("keepdir/inner", "inner") == 5);
    chdir("/");
    printf("ext2w: done, %d failed\n", bad);
    return bad;
}
