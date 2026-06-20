/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, kurumi
 *
 * See LICENSE for details.
 */

#define _GNU_SOURCE
#include "util.h"
#include <string.h>

void
secure_zero(void *s, size_t n)
{
        if (!s || n == 0)
                return;
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
        explicit_bzero(s, n);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
        explicit_bzero(s, n);
#else
        memset(s, 0, n);
#if defined(__GNUC__) || defined(__clang__)
        __asm__ volatile("" : : "r"(s) : "memory");
#else
        volatile unsigned char *p = s;
        while (n--)
                *p++ = 0;
#endif
#endif
}

int
parse_hex(const char *s, unsigned long *out)
{
        if (!s || !s[0])
                return -1;

        if (s[0] == '#') {
                s++;
                if (!s[0])
                        return -1;
        } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                s += 2;
                if (!s[0])
                        return -1;
        }

        unsigned long v = 0;
        size_t n = 0;
        for (; s[n]; n++) {
                if (n >= 6)
                        return -1;
                v <<= 4;
                if (s[n] >= '0' && s[n] <= '9')
                        v |= (unsigned long)(s[n] - '0');
                else if (s[n] >= 'a' && s[n] <= 'f')
                        v |= (unsigned long)(s[n] - 'a' + 10);
                else if (s[n] >= 'A' && s[n] <= 'F')
                        v |= (unsigned long)(s[n] - 'A' + 10);
                else
                        return -1;
        }
        if (n == 0)
                return -1;

        *out = v;
        return 0;
}
