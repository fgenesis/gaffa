#pragma once

#include "defs.h"

struct HStr
{
    uhash h;
    size_t len;
};


#define HASH_SEED 0  // FIXME


// hash and strlen() in one
HStr lenhash(size_t h, const char * const s);

// same hash but known length
uhash memhash(size_t h, const void *buf, size_t size);

uhash hashvalue(size_t h, ValU v);
