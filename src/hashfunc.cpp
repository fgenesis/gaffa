#include "hashfunc.h"


// hash and strlen() in one
HStr lenhash(uhash h, const char * const s)
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
uhash memhash(uhash h, const void *buf, size_t size)
{
    const unsigned char *p = (const unsigned char*)buf;
    do
        h = (h << 5u) + (h >> 2u) + *p++;
    while(--size);
    return h;
}

uhash hashvalue(uhash h, ValU v)
{
    h ^= rotl(0x811c9dc5, v.type);
    h += v.u.opaque;
    h ^= v.u.opaque >> 6u; // Improve binning for pointer values (which usually have the lower 2-3 bits at zero due to alignment)
    if(sizeof(v.u.opaque) > sizeof(uhash))
        h += v.u.opaque >> 29u;
    return h;
}
