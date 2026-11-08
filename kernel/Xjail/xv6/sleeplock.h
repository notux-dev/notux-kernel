#ifndef XV6_SLEEPLOCK_H
#define XV6_SLEEPLOCK_H
#include "spinlock.h"
struct sleeplock {
    int locked;
    const char *name;
    struct spinlock lk;
    int pid;
};
#endif
