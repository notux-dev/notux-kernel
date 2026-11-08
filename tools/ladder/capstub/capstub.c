#include <sys/capability.h>
#include <unistd.h>
#include <errno.h>
cap_iab_t cap_iab_from_text(const char *text) { (void)text; errno = EINVAL; return 0; }
int cap_iab_set_proc(cap_iab_t iab) { (void)iab; errno = EPERM; return -1; }
int cap_free(void *obj) { (void)obj; return 0; }
int cap_setuid(uid_t uid) { return setuid(uid); }
int cap_set_secbits(unsigned bits) { (void)bits; return 0; }
