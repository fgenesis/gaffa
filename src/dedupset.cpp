#include "dedupset.h"
#include "gc.h"
#include "hashfunc.h"
#include "util.h"
#include <string.h>

/* Deduplicating hash set for arbitrary memory blocks. Notes:
- Open addressing hash set for the keys + an extra array for the values.
- The values array can have holes (unlike the way class Table is implemented)
- Unused HBlocks may or may not be part of a freelist
- Uses short block optimization. Small memory blocks that fit are stored inline in a HBlock
- For long blocks, hashes are computed only once, on insert, and stored.
- For short blocks, hashes are not stored and instead recomputed as needed (since it's cheap enough)
- Items never move around in arr[], ie. their index stays constant (because that index is the ref)
*/


// The hashes in these are unused dummy values
static const Dedup::HBlock NullBlock = { {NULL, 0}, 0, {0}, Dedup::LONG_BIT };
static const Dedup::HBlock EmptyBlock = { {(const char*)&NullBlock, 0}, 0, {0}, Dedup::LONG_BIT };

enum { INITIAL_ELEMS = 32 };


static uhash keyhash(uhash h, const void *buf, size_t size)
{
    return memhash(h, buf, size) ^ rotr(uhash(size), 12); // Size gets mixed in as well
}



Dedup::Dedup(GC & gc, bool extrabyte, tsize skipAtStart)
    : keys(NULL), mask(-1), _extrabyte(extrabyte), _skipAtStart(skipAtStart), _hashseed(0), _nextfree(0)
    , _sweeppos(2), _inuse(0), gc(gc)
{
}

Dedup::~Dedup()
{
    dealloc();
}

bool Dedup::init()
{
    dealloc();
    if( arr.reserve(gc, INITIAL_ELEMS)
     && arr.push_back(gc, NullBlock)
     && arr.push_back(gc, EmptyBlock))
    {
        _hashseed = uhash((uintptr_t(arr.data()) >> 3u) ^ (uintptr_t(this) << 4u));
        return true;
    }

    return false;
}

void Dedup::dealloc()
{
    _kresize(0);
    arr.dealloc(gc);
}



sref Dedup::putCopy(const void* mem, size_t bytes)
{
    assert(arr.size() >= 2 && "Dedup: If this fails, you forgot to call init()");

    if(!bytes)
        return !!mem; // 0: NULL, 1: empty

    HKey * const k = _prepkey(mem, bytes);
    if(!k)
        return -1;

    if(k->ref >= 2)
        return k->ref;

    HBlock * const hb = _prepblock();
    if(!hb)
        return -1;

    if(!_acopy(*hb, mem, bytes))
    {
        _recycle(*hb);
        return -1;
    }

    return _finishset(*hb, *k);
}

sref Dedup::putTakeOver(void* mem, size_t bytes, size_t actualsize)
{
    assert(arr.size() >= 2 && "Dedup: If this fails, you forgot to call init()");

    if(!bytes)
    {
        if(actualsize)
            _afree(mem, actualsize);
        return !!mem; // 0: NULL, 1: empty
    }

    HKey * const k = _prepkey(mem, bytes);
    if(!k)
        return -1;

    if(k->ref >= 2)
    {
        _afree(mem, actualsize);
        return k->ref;
    }

    HBlock * const hb = _prepblock();
    if(!hb)
        return -1;

    _atakeover(*hb, mem, bytes, actualsize);
    return _finishset(*hb, *k);
}

tsize Dedup::_indexof(const HBlock& hb) const
{
    const size_t offs = &hb - arr.data();
    assert(offs < arr.size()); // block must be in used area of arr[]
    return offs;
}

sref Dedup::_finishset(HBlock& hb, HKey& k)
{
    if(!(hb.x & LONG_BIT))
        hb.h = k.h;
    const sref ref = _indexof(hb);
    assert(ref >= 2);
    k.ref = ref;
    return ref;
}

MemBlock Dedup::get(sref ref) const
{
    const unsigned char x = arr[ref].x;
    assert(x); // if this fails, then the block was GC'd
    if(x & LONG_BIT)
        return arr[ref].mb;

    MemBlock b = { (const char*)&arr[ref], size_t(x & SHRT_SIZE_MASK) };
    return b;
}

sref Dedup::find(const void* mem, size_t bytes) const
{
    assert(_skipAtStart <= bytes);
    mem = (const char*)mem + _skipAtStart;
    bytes -= _skipAtStart;

    if(!bytes)
        return !!mem;

    if(!keys)
        return 0;

    const uhash h = keyhash(_hashseed, mem, bytes);

    HKey * const k = keys;
    usize i = h;
    for(;;)
    {
        i &= mask;
        if(k[i].ref < 2)
            return 0; // open addressing chain broken -> key not found
        if(k[i].h == h)
        {
            const sref ref = k[i].ref;
            const HBlock& b = arr[ref];
            const unsigned char x = b.x;
            if(x & LONG_BIT)
            {
                assert(b.h == h);
                if(b.mb.n == bytes && !memcmp(mem, b.mb.p, bytes))
                    return ref;
            }
            else // the size check naturally fails when x==0 (ie. unused block) since we never end up here with bytes==0
            {
                tsize n = x & SHRT_SIZE_MASK;
                if(n == bytes && !memcmp(mem, &b, n))
                    return ref;
            }
        }
        ++i;
    }
}

Dedup::HKey *Dedup::_prepkey(const void* mem, size_t bytes)
{
    const tsize skip = _skipAtStart;
    assert(skip < bytes);
    mem = (const char*)mem + skip;
    bytes -= skip;

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
            const char *m;
            tsize n;
            if(x & LONG_BIT)
            {
                assert(b.h == h);
                n = b.mb.n;
                m = b.mb.p;
            }
            else // the size check naturally fails when x==0 (ie. unused block) since we never end up here with bytes==0
            {
                n = x & SHRT_SIZE_MASK;
                m = (const char*)&b;
            }

            if(n == bytes + skip && !memcmp(mem, m + skip, n - skip))
                goto done;
        }
        ++i;
    }

    // Found empty slot.
    k[i].h = h;
done:
    return &k[i];
}

Dedup::HBlock* Dedup::_prepblock()
{
    tsize nu = _nextfree;
    if(nu && nu < arr.sz) // Part of the freelist?
    {
        assert(arr[nu].x == 0); // must be unused
        _nextfree = (tsize)arr[nu].mb.n; // pop from freelist
        return &arr[nu];
    }

    return arr.alloc_n(gc, 1);
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
    const HBlock * const end = rd + arr.size() - 2;

    for( ; rd < end; ++rd)
    {
        if(!rd->x)
            continue;

        *w = *rd;
        ++w;
    }
    const tsize inuse = w - arr.data();
    assert(inuse >= 2);
    if(inuse < arr.size())
        (void)arr.resize(gc, inuse); // this can fail but without consequences

    // Invariant: The freelist is gone after compacting, since no unused blocks are left
    _nextfree = 0;
}

// fallback for allocated blocks but that ended up not being used
void Dedup::_recycle(HBlock& hb)
{
    assert(!hb.x && !hb.mb.p); // block must be already cleared
    hb.mb.n = _nextfree; // add to freelist
    _nextfree = _indexof(hb);
}

// Shrinking is only called during GC run
// Enlarging can happen anytime
Dedup::HKey* Dedup::_kresize(tsize newsize)
{
    // TODO: make sure that newsize is power of 2
    const tsize oldsize = mask + 1;
    const tsize newmask = newsize - 1;
    HKey * const newks = gc_alloc_unmanaged_zero_T<HKey>(gc, newsize);
    if(!newks && newsize)
        return oldsize > newsize ? keys : NULL; // Shrinking but can't alloc smaller buffer? keep old.

    gc_alloc_unmanaged_T(gc, keys, oldsize, 0);
    keys = newks;
    mask = newmask;

    if(newsize < oldsize && oldsize && newsize) // When shrinking, compact valid entries
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
        hb.x = (unsigned char)bytes | MARK_BIT;
    }
    else
    {
        p = (char*)gc_alloc_unmanaged(gc, NULL, 0, total);
        if(!p)
            return false;
        hb.x = LONG_BIT | MARK_BIT; // new strings start as marked as to not get GC'd immediately
        hb.extraBytesToFree = _extrabyte;
    }
    p[total - 1] = 0; // may get overwritten by the memcpy()
    memcpy(p, mem, bytes);
    return true;
}

void Dedup::_atakeover(HBlock& hb, void* mem, size_t bytes, size_t actualsize)
{
    size_t total = bytes + _extrabyte;
    assert(total <= actualsize);
    if(total <= MAX_SHORT_LEN)
    {
        hb.x = (unsigned char)bytes | MARK_BIT;
        char *p = (char*)&hb;
        assert(!_extrabyte || p[total - 1]); // strings should be 0-terminated when taking over
        memcpy(p, mem, bytes);
        _afree(mem, actualsize); // Don't need the allocation anymore if it fit into a short block
    }
    else
    {
        size_t diff = actualsize - bytes;
        assert(diff <= 0xff);
        hb.extraBytesToFree = diff;
        hb.mb.p = (char*)mem;
        hb.mb.n = bytes;
        hb.x = LONG_BIT | MARK_BIT; // new strings start as marked as to not get GC'd immediately
    }
}

void Dedup::_afree(void* mem, size_t bytes)
{
    gc_alloc_unmanaged(gc, mem, bytes, 0);
}

void Dedup::mark(sref ref)
{
    assert(arr[ref].x); // block was already sweeped
    arr[ref].x |= MARK_BIT;
}

tsize Dedup::sweepstep(tsize step)
{
    assert(_sweeppos >= 2);
    const tsize N = arr.size();
    HBlock * const bs = arr.data();
    tsize i = _sweeppos;
    tsize c = 0;
    tsize nu = _nextfree;
    for( ; i < N; ++i) // _sweeppos is always >= 2 -> skip sentinel values
    {
        HBlock& b = bs[i];
        const unsigned x = b.x;
        if(x & MARK_BIT) // Block is in use?
        {
            b.x = x & ~MARK_BIT; // Won't collect it this time.
            ++c;
        }
        else
        {
            // Block is unused; mark it as such and free external mem if present.
            // Note that this keeps a HKey pointing at this block and we have no cheap way of getting rid of it.
            // The idea is that we're in the middle of a collection that will eventually finish.
            // In case new blocks are requested before the collection finishes, this block may be re-used,
            // and it gets another, fresh HKey pointing at it.
            // Meanwhile, following a stale HKey to this block must correctly detect this as
            // "this is not the block you're looking for" and continue searching -- see _prepkey().
            // Once the collection finishes and the compaction step takes place, residual HKey entries are removed.
            const MemBlock mb = b.mb; // make a copy before overwriting it
            b.x = 0; // mark as unused
            b.mb.p = NULL; // prevent dangling pointer just in case
            b.mb.n = nu; // chain block into freelist
            nu = i;
            if(x & LONG_BIT)
            {
                _afree((void*)mb.p, mb.n + b.extraBytesToFree);
                if(!--step) // one free costs one step; steps=0 underflows and causes a full sweep
                    break;
            }
        }
    }
    _sweeppos = i;
    _inuse += c;
    _nextfree = nu;
    return step; // steps > 0? full cycle finished
}

void Dedup::sweepfinish(bool compact)
{
    if(compact)
    {
        const size_t sz = size_t(mask + 1); // possibly overflow, then cast
        size_t target = roundUpToPowerOfTwo(size_t(_inuse) * 2);
        if(target < INITIAL_ELEMS)
            target = INITIAL_ELEMS;
        if(target < sz)
            _kresize(target);
    }
    _sweeppos = 2;
    _inuse = 0;
}
