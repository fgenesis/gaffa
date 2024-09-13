#include "dedupset.h"
#include "gc.h"
#include "hashfunc.h"
#include <string.h>

/* Deduplicating hash set. Notes:
- Hashes are computed only once, on insert, and stored.
- Open addressing hash set for the keys + an extra array for the values.
- The values array can have holes
- Items never change index in arr[] (because that index is the ref)
-
*/


// The hashes in these are unused dummy values
static const Dedup::HBlock NullBlock = { {NULL, 0}, 0 };
static const Dedup::HBlock EmptyBlock = { {(const char*)&NullBlock, 0}, 0 };

enum { INITIAL_ELEMS = 32 };


// The idea behind this hash function is to use the upper 12 bits solely as size,
// and the remaining lower bits as hashmap index
static uhash keyhash(uhash h, const void *buf, size_t size)
{
    h = memhash(h, buf, size);
    h = (h >> 12u) ^ (h & ((1 << 12) - 1));
    h ^= rotr(uhash(size), 12); // The low part of the size goes in the upper bits
    return h;
}



Dedup::Dedup(GC & gc, bool extrabyte)
    : keys(NULL), mask(-1), _gc(gc), _extrabyte(extrabyte), _hashseed(0)
{
}

Dedup::~Dedup()
{
    dealloc();
}

bool Dedup::init()
{
    dealloc();
    if( arr.reserve(_gc, INITIAL_ELEMS)
     && arr.push_back(_gc, NullBlock)
     && arr.push_back(_gc, EmptyBlock))
    {
        _hashseed = uhash((uintptr_t(arr.data()) >> 3u) ^ (uintptr_t(this) << 4u));
        return true;
    }

    return false;
}

void Dedup::dealloc()
{
    if(keys)
    {
        _kresize(0);
        arr.dealloc(_gc);
    }
}

sref Dedup::putCopy(const void* mem, size_t bytes)
{
    if(!bytes)
        return !!mem; // 0: NULL, 1: empty

    HKey * const k = _prepkey(mem, bytes);
    if(!k)
        return -1;

    if(k->ref >= 2)
        return k->ref;

    MemBlock cp = _acopy(mem, bytes);
    if(!cp.p)
        return -1;

    HBlock hb = { cp, k->h };
    if(!arr.push_back(_gc, hb))
    {
        _afree((void*)cp.p, cp.n);
        return -1;
    }

    sref ref = arr.size() - 1;
    assert(ref >= 2);
    k->ref = ref;
    return ref;
}

sref Dedup::putTakeOver(void* mem, size_t bytes)
{
    if(!bytes)
        return !!mem; // 0: NULL, 1: empty

    HKey * const k = _prepkey(mem, bytes);
    if(!k)
        return -1;

    if(k->ref >= 2)
    {
        _afree(mem, bytes);
        return k->ref;
    }

    MemBlock mb = { (const char*)mem, bytes };
    HBlock hb = { mb, k->h };
    if(!arr.push_back(_gc, hb))
        return -1;

    sref ref = arr.size() - 1;
    assert(ref >= 2);
    k->ref = ref;
    return ref;
}

Dedup::HKey *Dedup::_prepkey(const void* mem, size_t bytes)
{
    HKey * const k = _kchecksize();
    if(!k)
        return NULL;
    const uhash h = keyhash(_hashseed, mem, bytes);

    usize i = h;
    for(;;) // There are no tombstones here. Once we hit an empty key, use that.
    {
        i &= mask;
        if(k[i].ref < 2)
            break;
        if(k[i].h == h)
        {
            const HBlock& b = arr[k[i].ref];
            assert(b.h == h);
            if(b.mb.n == bytes && !memcmp(mem, b.mb.p, bytes))
                return &k[i];
        }
    }

    // Found empty slot.
    k[i].h = h;
    return &k[i];
}

Dedup::HKey* Dedup::_kchecksize()
{
    if(!keys)
        return _kresize(INITIAL_ELEMS);
    if(arr.size() + (arr.size() / 4u) >= mask)
        return _kresize((mask + 1) * 2);
    return keys;
}

// Shrinking is only called during GC run
// Enlarging can happen anytime
Dedup::HKey* Dedup::_kresize(tsize newsize)
{
    // TODO: make sure that newsize is power of 2
    const tsize oldsize = mask + 1;
    const tsize newmask = newsize - 1;
    HKey * const newks = gc_alloc_unmanaged_zero_T<HKey>(_gc, newsize);
    if(!newks && newsize)
        return oldsize > newsize ? keys : NULL; // Shrinking but can't alloc smaller buffer? keep old.

    gc_alloc_unmanaged_T(_gc, keys, oldsize, 0);
    keys = newks;
    mask = newmask;

    if(oldsize && newsize)
    {
        if(newsize < oldsize) // When shrinking, compact valid entries
        {
            HBlock *w = arr.data() + 2;
            const HBlock *rd = w;
            const HBlock * const end = rd + arr.size();

            for( ; rd < end; ++rd)
            {
                if(!rd->mb.p)
                    continue;

                *w = *rd;
                ++w;
            }
            const tsize inuse = end - w;
            const tsize halfcap = arr.cap / 2u;
            if(inuse < halfcap && inuse >= INITIAL_ELEMS) // shrink when more than half is unused
                (void)arr.resize(_gc, halfcap); // this can fail but without consequences

            arr.sz = inuse;
        }

        // Regenerate keys from hashed blocks
        const tsize N = arr.size();
        for(tsize j = 2; j < N; ++j)
        {
            const uhash h = arr[j].h;
            usize i = h;
            for(;;)
            {
                i &= newmask;
                if(newks[i].ref < 2)
                {
                    newks[i].h = h;
                    newks[i].ref = j;
                }
                ++i;
            }
        }
    }

    return newks;
}

MemBlock Dedup::_acopy(const void* mem, size_t bytes)
{
    size_t total = bytes + _extrabyte;
    char *p = (char*)gc_alloc_unmanaged(_gc, NULL, 0, total);
    p[total - 1] = 0; // may get overwritten by the next line
    memcpy(p, mem, bytes);
    MemBlock mb { (char*)p, bytes };
    return mb;
}

void Dedup::_afree(void* mem, size_t bytes)
{
    gc_alloc_unmanaged(_gc, mem, bytes + _extrabyte, 0);
}

void Dedup::mark(sref ref)
{
    assert(false); // TODO
}

void Dedup::sweep()
{
    assert(false); // TODO
}
