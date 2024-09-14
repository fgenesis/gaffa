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
static const Dedup::HBlock NullBlock = { {NULL, 0}, 0, {0}, Dedup::LONG_BIT };
static const Dedup::HBlock EmptyBlock = { {(const char*)&NullBlock, 0}, 0, {0}, Dedup::LONG_BIT };

enum { INITIAL_ELEMS = 32 };


static uhash keyhash(uhash h, const void *buf, size_t size)
{
    return memhash(h, buf, size) ^ rotr(uhash(size), 12); // Size gets mixed in as well
}



Dedup::Dedup(GC & gc, bool extrabyte)
    : keys(NULL), mask(-1), _gc(gc), _extrabyte(extrabyte), _hashseed(0), _sweeppos(2)
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
    _kresize(0);
    arr.dealloc(_gc);
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

    HBlock *hb = arr.push_back(_gc, HBlock());
    if(!hb)
        return -1;

    if(!_acopy(*hb, mem, bytes))
    {
        --arr.sz;
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

MemBlock Dedup::get(sref ref) const
{
    unsigned char x = arr[ref].x;
    if(x & LONG_BIT)
        return arr[ref].mb;
    
    MemBlock b = { (const char*)&arr[ref], x & SHRT_SIZE_MASK };
    return b;
}

Dedup::HKey *Dedup::_prepkey(const void* mem, size_t bytes)
{
    HKey *k = keys;
    if(!k)
        k = _kresize(INITIAL_ELEMS);
    else if(arr.size() + (arr.size() / 4u) >= mask)
        k = _kresize((mask + 1) * 2);
    if(!k)
        return NULL;
    const uhash h = keyhash(_hashseed, mem, bytes);

    usize i = h;
    for(;;)
    {
        i &= mask;
        if(k[i].ref < 2) // There are no tombstones here. Once we hit an empty key, use that.
            break;
        if(k[i].h == h)
        {
            const HBlock& b = arr[k[i].ref];
            unsigned char x = b.x;
            if(x & LONG_BIT)
            {
                assert(b.h == h);
                if(b.mb.n == bytes && !memcmp(mem, b.mb.p, bytes))
                    return &k[i];
            }
            else
            {
                tsize n = x & SHRT_SIZE_MASK;
                if(n == bytes && !memcmp(mem, &b, n))
                    return &k[i];
            }
        }
        ++i;
    }

    // Found empty slot.
    k[i].h = h;
    return &k[i];
}

void Dedup::_regenkeys(HKey *ks, tsize newmask)
{
    // Regenerate keys from hashed blocks
    // Precond: ks is all zeros
    const tsize N = arr.size();
    for(tsize j = 2; j < N; ++j) // skip sentinel values
    {
        unsigned char x = arr[j].x;
        const uhash h = x & LONG_BIT
            ? arr[j].h // Long blocks store the hash because recopmuting it may take a while
            : keyhash(_hashseed, &arr[j], x & SHRT_SIZE_MASK); // Hashes of short blocks must be recomputed
        usize i = h;
        for(;;)
        {
            i &= newmask;
            if(ks[i].ref < 2)
            {
                ks[i].h = h;
                ks[i].ref = j;
                break;
            }
            ++i;
        }
    }
}

void Dedup::_compact()
{
    HBlock *w = arr.data() + 2; // skip sentinel values
    const HBlock *rd = w;
    const HBlock * const end = rd + arr.size();

    for( ; rd < end; ++rd)
    {
        if(!rd->x)
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

    if(newsize < oldsize && oldsize) // When shrinking, compact valid entries
    {
        _compact();
        _regenkeys(newks, newmask);
    }

    return newks;
}

bool Dedup::_acopy(HBlock& hb, const void* mem, size_t bytes)
{
    size_t total = bytes + _extrabyte;
    char *p;
    if(total <= MAX_SHORT_LEN)
    {
        p = (char*)&hb;
        hb.x = (unsigned char)bytes;
    }
    else
    {
        p = (char*)gc_alloc_unmanaged(_gc, NULL, 0, total);
        if(!p)
            return false;
        p[total - 1] = 0; // may get overwritten by the memcpy()
        hb.x = LONG_BIT | MARK_BIT; // new strings start as marked as to not get GC'd immediately
    }

    memcpy(p, mem, bytes);
    return true;
}

void Dedup::_afree(void* mem, size_t bytes)
{
    gc_alloc_unmanaged(_gc, mem, bytes + _extrabyte, 0);
}

void Dedup::mark(sref ref)
{
    assert(arr[ref].x); // block was already sweeped
    arr[ref].x |= MARK_BIT;
}

bool Dedup::sweepstep(tsize step)
{
    assert(_sweeppos >= 2);
    const tsize N = arr.size();
    HBlock * const bs = arr.data();
    tsize i = _sweeppos;
    tsize c = 0;
    for( ; i < N; ++i) // skip sentinel values
    {
        const unsigned x = bs[i].x;
        if(x & MARK_BIT)
        {
            bs[i].x = x & ~MARK_BIT;
            ++c;
        }
        else
        {
            bs[i].x = 0; // mark as deletable
            if(x & LONG_BIT)
            {
                MemBlock b = bs[i].mb;
                _afree((void*)b.p, b.n); // one free costs one step
                if(!--step) // steps=0 underflows and causes a full sweep
                    break;
            }
        }
    }
    _sweeppos = i;
    _inuse += c;
    return !!step; // steps left? full cycle finished
}

void Dedup::sweepfinish()
{
    const tsize sz = mask + 1;
    if(_inuse * 4 < sz)
        _kresize(sz / 2u);
    _sweeppos = 2;
    _inuse = 0;
}
