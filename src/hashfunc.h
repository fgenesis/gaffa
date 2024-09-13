#pragma once

#include "defs.h"
#include "util.h"
#include <limits.h> // CHAR_BIT

struct HStr
{
    uhash h;
    size_t len;
};


#define HASH_SEED 0  // FIXME


FORCEINLINE static uhash rotl(uhash h, unsigned n)
{
    enum { Bits = CHAR_BIT * sizeof(uhash), Mask = Bits - 1 };
    n &= Mask;
    return (h << n) | (h >> (Bits - n));
}

FORCEINLINE static uhash rotr(uhash h, unsigned n)
{
    enum { Bits = CHAR_BIT * sizeof(uhash), Mask = Bits - 1 };
    n &= Mask;
    return (h >> n) | (h << (Bits - n));
}


// hash and strlen() in one
HStr lenhash(uhash h, const char * const s);

// same hash but known length
uhash memhash(uhash h, const void *buf, size_t size);

uhash hashvalue(uhash h, ValU v);
