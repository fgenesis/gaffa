#include "util.h"

MaybeNum strtouint(const char* s, size_t maxlen)
{
    return strtouint_dec(s, maxlen);
}

MaybeNum strtouint_dec(const char* s, size_t maxlen)
{
    MaybeNum res;
    res.overflow = false;
    res.used = 0;
    res.val.ui = 0;

    if(maxlen) do
    {
        unsigned char c = s[res.used];
        if(c >= '0' && c <= '9')
        {
            ++res.used;
            res.overflow |= mul_check_overflow<uint>(&res.val.ui, res.val.ui, 10);
            res.overflow |= add_check_overflow<uint>(&res.val.ui, res.val.ui, c - '0');
        }
        else
            break;
    }
    while(res.used < maxlen);

    return res;
}
