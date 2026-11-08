#ifndef UACCESS_H
#define UACCESS_H
#include "stdint.h"
#include "process.h"
int copy_from_user(process_t *p, void *dst, uint64_t va, uint64_t n);
int copy_to_user(process_t *p, uint64_t va, const void *src, uint64_t n);
int strncpy_from_user(process_t *p, char *dst, uint64_t va, int max);
#endif
