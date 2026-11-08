#include "types.h"
void *
memmove(void *vdst, const void *vsrc, uint n)
{
    char *dst = (char *)vdst;
    const char *src = (const char *)vsrc;
    if (n == 0 || dst == src)
        return vdst;
    if (src < dst && src + n > dst) {
        src += n;
        dst += n;
        while (n-- > 0)
            *--dst = *--src;
    } else {
        while (n-- > 0)
            *dst++ = *src++;
    }
    return vdst;
}
void *
memset(void *dst, int c, uint n)
{
    unsigned char *p = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;
    uint i;
    for (i = 0; i < n; i++)
        p[i] = v;
    return dst;
}
int
strncmp(const char *p, const char *q, uint n)
{
    while (n > 0 && *p != 0 && *p == *q) {
        n--;
        p++;
        q++;
    }
    if (n == 0)
        return 0;
    return (int)((unsigned char)*p) - (int)((unsigned char)*q);
}
char *
strncpy(char *s, const char *t, int n)
{
    char *os = s;
    while (n > 0 && (*s = *t) != 0) {
        s++;
        t++;
        n--;
    }
    while (n > 0) {
        *s = 0;
        s++;
        n--;
    }
    return os;
}
