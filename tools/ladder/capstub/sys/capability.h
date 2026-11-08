#ifndef NOTUX_CAPSTUB_H
#define NOTUX_CAPSTUB_H
#include <sys/types.h>
typedef struct notux_cap_iab *cap_iab_t;
cap_iab_t cap_iab_from_text(const char *text);
int cap_iab_set_proc(cap_iab_t iab);
int cap_free(void *obj);
int cap_setuid(uid_t uid);
int cap_set_secbits(unsigned bits);
#endif
