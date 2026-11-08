#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int bad;
#define CHECK(c) do { int ok_ = !!(c); printf("fs: %-34s %s\n", #c, ok_ ? "ok" : "FAIL"); if (!ok_) bad++; } while (0)

int main(void)
{
    char cwd[128];
    CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/"));

    DIR *d = opendir("/");
    CHECK(d != NULL);
    int saw_bin = 0, saw_readme = 0, n = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) {
        n++;
        if (!strcmp(e->d_name, "bin")) saw_bin = 1;
        if (!strcmp(e->d_name, "README")) saw_readme = 1;
        printf("fs: entry '%s'\n", e->d_name);
    }
    if (d) closedir(d);
    CHECK(n >= 4 && saw_bin && saw_readme);

    struct stat st;
    CHECK(stat("/README", &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 50);
    CHECK(stat("/bin", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(stat("/nonexistent", &st) != 0);

    CHECK(chdir("/bin") == 0);
    CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/bin"));
    CHECK(access("child", F_OK) == 0);
    CHECK(stat("child", &st) == 0 && st.st_size > 0);
    CHECK(chdir("/README") != 0);
    CHECK(chdir("..") == 0);
    CHECK(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/"));

    FILE *f = fopen("/README", "r");
    char line[64] = "";
    CHECK(f && fgets(line, sizeof line, f) && !strncmp(line, "Welcome to notux", 16));
    if (f) fclose(f);

    int fd = open("/README", O_RDONLY);
    char buf[8] = "";
    CHECK(fd >= 0 && pread(fd, buf, 7, 11) == 7 && !strncmp(buf, "notux.\n", 7));
    CHECK(fstat(fd, &st) == 0 && st.st_size > 50);
    close(fd);

    CHECK(chdir("/etc") == 0);
    pid_t c = fork();
    if (c == 0) {
        char w[64];
        _exit(getcwd(w, sizeof w) && !strcmp(w, "/etc") ? 0 : 1);
    }
    int status = -1;
    waitpid(c, &status, 0);
    CHECK(status == 0);

    CHECK(stat("/tmp", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(stat("/var/log", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(stat("/lib", &st) == 0 && S_ISDIR(st.st_mode) && stat("/run", &st) == 0 && stat("/usr", &st) == 0);
    {
        char tpl[] = "/tmp/fsXXXXXX";
        int tf = mkstemp(tpl);
        CHECK(tf >= 0 && write(tf, "tmp", 3) == 3 && stat(tpl, &st) == 0 && st.st_size == 3);
        if (tf >= 0) { close(tf); CHECK(unlink(tpl) == 0 && stat(tpl, &st) != 0); }
        FILE *lg = fopen("/var/log/test.log", "w");
        CHECK(lg && fputs("log line\n", lg) >= 0 && fclose(lg) == 0 && stat("/var/log/test.log", &st) == 0);
        unlink("/var/log/test.log");
    }

    printf("fs: failures=%d\n", bad);
    return bad;
}
