#include "util.h"

int
parse_hex(const char *s, unsigned long *out)
{
        if (!s || !s[0])
                return -1;

        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
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
