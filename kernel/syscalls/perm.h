#ifndef PERM_H
#define PERM_H 
#include "process.h"
struct inode;
#define P_R 4
#define P_W 2
#define P_X 1
int perm_inode(process_t *p, struct inode *ip, int want);
int perm_inode_l(process_t *p, struct inode *ip, int want);
int perm_inode_ids(uint32_t uid, uint32_t gid, process_t *p, struct inode *ip, int want, int locked);
int perm_parent(process_t *p, const char *path, int want);
int perm_remove(process_t *p, const char *path);
int perm_owner(process_t *p, struct inode *ip);
int perm_can_kill(process_t *from, process_t *to);
int perm_in_group(process_t *p, uint32_t gid);
int perm_inode_real(process_t *p, struct inode *ip, int want);
static inline int perm_root(process_t *p) { return p->cred.euid == 0; }
extern int ext2_new_uid, ext2_new_gid;
void perm_set_creator(process_t *p);
void perm_clear_creator(void);
#endif
