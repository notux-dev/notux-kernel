#ifndef XV6_SPINLOCK_H
#define XV6_SPINLOCK_H
struct spinlock {
    int locked;
    const char *name;
};
#endif
