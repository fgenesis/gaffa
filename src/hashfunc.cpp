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
    h ^= rotl(0x811c9dc5, v.type.id);
	h += v.u.opaque;
    if(sizeof(v.u.opaque) > sizeof(uhash))
        h += h >> 29u;
	return (uhash)h;
}
