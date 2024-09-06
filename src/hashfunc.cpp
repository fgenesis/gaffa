#include "hashfunc.h"

static inline uhash rotl(uhash h, unsigned n)
{
    enum { Bits = CHAR_BIT * sizeof(uhash) };
    n &= Bits - 1;
    return (h << n) | (h >> (Bits - n));
}

// hash and strlen() in one
HStr lenhash(size_t h, const char * const s)
{
    const char *p = s;
    for(;;)
    {
        unsigned char c = (unsigned char)*p++;
        if(!c)
            break;
        h = (h << 5u) + (h >> 2u) + c;
    }

    HStr hs;
    hs.len = (const char*)p - s - 1;
    hs.h = h;
    return hs;
}

// same hash but known length
uhash memhash(size_t h, const void *buf, size_t size)
{
    const unsigned char *p = (const unsigned char*)buf;
    do
        h = (h << 5u) + (h >> 2u) + *p++;
    while(--size);
    return h;
}

uhash hashvalue(size_t h, ValU v)
{
    h ^= rotl(0x811c9dc5, v.type.id);
	h += v.u.opaque;
	return h;
}
