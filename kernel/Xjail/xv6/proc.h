#ifndef XV6_PROC_H
#define XV6_PROC_H
struct inode;
struct proc {
    struct inode *cwd;
};
#endif
