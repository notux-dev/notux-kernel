#define _GNU_SOURCE
#include "xvsx_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t parse_size(const char *s)
{
    char *end;
    uint64_t v = strtoull(s, &end, 10);
    switch (*end) {
    case 'k': case 'K': v <<= 10; break;
    case 'm': case 'M': v <<= 20; break;
    case 'g': case 'G': v <<= 30; break;
    }
    return v;
}

static uint32_t parent_of(xvsx_t *x, const char *path, const char **leaf)
{
    static char tmp[1024];
    strncpy(tmp, path, sizeof tmp - 1);
    char *slash = strrchr(tmp, '/');
    if (!slash) {
        *leaf = tmp;
        return XVSX_ROOT_INO;
    }
    *leaf = slash + 1;
    if (slash == tmp)
        return XVSX_ROOT_INO;
    *slash = 0;
    return xvsx_lookup(x, tmp);
}

static void *slurp(const char *path, uint64_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc(n ? (size_t)n : 1);
    if (!buf || (n && fread(buf, 1, (size_t)n, f) != (size_t)n)) {
        perror(path);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (uint64_t)n;
    return buf;
}

static const char readme_text[] =
    "Welcome to notux.\n"
    "\n"
    "This file was written by tools/xvsx/mkfs.xvsx when this disk image\n"
    "was built -- if you can read this, the AHCI driver and\n"
    "xvsXfs are both working end to end.\n"
    "\n"
    "Layout:\n"
    "  /bin   executables\n"
    "  /etc   configuration\n"
    "  /root  root's home directory\n";

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s IMAGE SIZE [--label L] [--default-layout] "
                "[--mkdir /p] [--add /p=file] [--symlink /p=target] "
                "[--max-run N]\n", argv[0]);
        return 2;
    }
    const char *label = "notux";
    uint32_t max_run = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--label") && i + 1 < argc) label = argv[++i];
        else if (!strcmp(argv[i], "--max-run") && i + 1 < argc) max_run = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--default-layout") ||
                 !strcmp(argv[i], "--mkdir") || !strcmp(argv[i], "--add") ||
                 !strcmp(argv[i], "--symlink")) {
            if (strcmp(argv[i], "--default-layout")) i++;
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    xvsx_t x;
    if (xvsx_format(&x, argv[1], parse_size(argv[2]), label))
        return 1;
    x.max_run = max_run;
    for (int i = 3; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--label") || !strcmp(a, "--max-run")) { i++; continue; }
        if (!strcmp(a, "--default-layout")) {
            const char *d[] = {"bin", "etc", "root", "lost+found"};
            for (int k = 0; k < 4; k++)
                if (!xvsx_mkdir(&x, XVSX_ROOT_INO, d[k], 0755)) {
                    fprintf(stderr, "mkdir %s failed\n", d[k]);
                    return 1;
                }
            if (!xvsx_mkfile(&x, XVSX_ROOT_INO, "README", readme_text,
                             strlen(readme_text), 0644)) {
                fprintf(stderr, "README failed\n");
                return 1;
            }
            continue;
        }
        const char *arg = argv[++i];
        if (!strcmp(a, "--mkdir")) {
            const char *leaf;
            uint32_t p = parent_of(&x, arg, &leaf);
            if (!p || !xvsx_mkdir(&x, p, leaf, 0755)) {
                fprintf(stderr, "mkdir %s failed\n", arg);
                return 1;
            }
        } else {
            char *eq = strchr(arg, '=');
            if (!eq) { fprintf(stderr, "%s needs /path=value\n", a); return 2; }
            char path[1024];
            snprintf(path, sizeof path, "%.*s", (int)(eq - arg), arg);
            const char *leaf;
            uint32_t p = parent_of(&x, path, &leaf);
            if (!p) { fprintf(stderr, "no parent for %s\n", path); return 1; }
            if (!strcmp(a, "--symlink")) {
                if (!xvsx_mksymlink(&x, p, leaf, eq + 1)) {
                    fprintf(stderr, "symlink %s failed\n", path);
                    return 1;
                }
            } else {
                uint64_t len;
                void *data = slurp(eq + 1, &len);
                if (!data)
                    return 1;
                uint32_t ino = xvsx_mkfile(&x, p, leaf, data, len, 0755);
                free(data);
                if (!ino) {
                    fprintf(stderr, "add %s failed\n", path);
                    return 1;
                }
                printf("mkfs.xvsx: %s <- %s (%llu bytes)\n", path, eq + 1,
                       (unsigned long long)len);
            }
        }
    }
    printf("mkfs.xvsx: %s: %llu blocks, %u inodes, %llu blocks free\n",
           argv[1], (unsigned long long)x.sb.total_blocks, x.sb.total_inodes,
           (unsigned long long)x.sb.free_blocks);
    return xvsx_close(&x) ? 1 : 0;
}
