#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("ext2: %-60s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

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
    struct stat st;
    if (stat("/mnt/hello.txt", &st) != 0) { printf("ext2: no ext2 disk attached, skipped\n"); return 0; }

    CHECK(slurp("/mnt/hello.txt", b, sizeof b) == 16 && !strcmp(b, "hello from ext2\n"));
    CHECK(S_ISREG(st.st_mode) && st.st_size == 16 && (st.st_mode & 0777) == 0644);
    CHECK(slurp("/mnt/a_rather_long_file_name_over_fourteen.txt", b, sizeof b) == 16 && !strncmp(b, "long name", 9));
    CHECK(slurp("/mnt/dir1/dir2/deep.txt", b, sizeof b) == 5 && !strcmp(b, "deep\n"));
    CHECK(stat("/mnt/dir1/dir2", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(stat("/mnt/nonexistent", &st) == -1 && errno == ENOENT);
    CHECK(stat("/mnt/hello.txt/x", &st) == -1 && errno == ENOTDIR);

    {
        int fd = open("/mnt/big.bin", O_RDONLY);
        long total = 0, badb = 0; unsigned char buf[3000]; int r;
        while ((r = read(fd, buf, sizeof buf)) > 0) {
            for (int i = 0; i < r; i++) {
                long k = total + i;
                if (buf[i] != (unsigned char)((k ^ (k >> 8) ^ (k >> 16)) & 0xff)) badb++;
            }
            total += r;
        }
        CHECK(fd >= 0 && total == 3 * 1024 * 1024 + 123 && badb == 0);
        unsigned char x[16];
        long off = 2500000;
        CHECK(pread(fd, x, 16, off) == 16 && x[0] == (unsigned char)((off ^ (off >> 8) ^ (off >> 16)) & 0xff));
        CHECK(lseek(fd, -1, SEEK_END) == 3 * 1024 * 1024 + 122);
        close(fd);
    }
    {
        int fd = open("/mnt/sparse.bin", O_RDONLY);
        unsigned char z[512]; long nz = 0;
        for (int i = 0; i < 8; i++) { pread(fd, z, 512, 1000000 + i * 512); for (int j = 0; j < 512; j++) nz += z[j]; }
        CHECK(fd >= 0 && nz == 0 && fstat(fd, &st) == 0 && st.st_size == 2097153);
        close(fd);
    }
    {
        DIR *d = opendir("/mnt/empty");
        int n = 0, seen17 = 0; struct dirent *e;
        while (d && (e = readdir(d))) {
            if (!strcmp(e->d_name, "file_number_17.txt")) seen17 = 1;
            if (strncmp(e->d_name, "file_number_", 12) == 0) n++;
        }
        CHECK(d && n == 60 && seen17);
        if (d) closedir(d);
        d = opendir("/mnt");
        int hello = 0, lng = 0, lnk = 0;
        while (d && (e = readdir(d))) {
            if (!strcmp(e->d_name, "hello.txt")) hello = (e->d_type == DT_REG);
            if (!strcmp(e->d_name, "a_rather_long_file_name_over_fourteen.txt")) lng = 1;
            if (!strcmp(e->d_name, "link_rel")) lnk = (e->d_type == DT_LNK);
        }
        CHECK(hello && lng && lnk);
        if (d) closedir(d);
    }
    {
        char t[256];
        CHECK(readlink("/mnt/link_rel", t, sizeof t) == 9 && !strncmp(t, "hello.txt", 9));
        CHECK(readlink("/mnt/link_abs", t, sizeof t) == 23 && !strncmp(t, "/mnt/dir1/dir2/deep.txt", 23));
        CHECK(lstat("/mnt/link_rel", &st) == 0 && S_ISLNK(st.st_mode));
        CHECK(stat("/mnt/link_rel", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 16);
        CHECK(slurp("/mnt/link_rel", b, sizeof b) == 16 && !strcmp(b, "hello from ext2\n"));
        CHECK(slurp("/mnt/link_abs", b, sizeof b) == 5 && !strcmp(b, "deep\n"));
        CHECK(slurp("/mnt/link_dir/dir2/deep.txt", b, sizeof b) == 5);
        CHECK(stat("/mnt/dangling", &st) == -1 && errno == ENOENT);
        CHECK(lstat("/mnt/dangling", &st) == 0 && S_ISLNK(st.st_mode));
        CHECK(stat("/mnt/loop_a", &st) == -1);
        CHECK(readlink("/mnt/hello.txt", t, sizeof t) == -1 && errno == EINVAL);
    }
    {
        char cwd[256];
        CHECK(chdir("/mnt/dir1/dir2") == 0 && getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/mnt/dir1/dir2"));
        CHECK(slurp("deep.txt", b, sizeof b) == 5);
        CHECK(chdir("../..") == 0 && getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/mnt"));
        CHECK(chdir("..") == 0 && getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/"));
        CHECK(chdir("/mnt/./dir1/../hello.txt") == -1);
        CHECK(slurp("/mnt/dir1/../hello.txt", b, sizeof b) == 16);
        chdir("/");
    }
    printf("ext2: done, %d failed\n", bad);
    return bad;
}
