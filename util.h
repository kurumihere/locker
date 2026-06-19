#ifndef LOCKER_UTIL_H
#define LOCKER_UTIL_H

#include <stddef.h>

int parse_hex(const char *s, unsigned long *out);
void secure_zero(void *s, size_t n);

#endif
