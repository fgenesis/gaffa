#pragma once

#include "defs.h"
#include "array.h"

struct GC;

enum
{
    REF_NULL = 0,
    REF_EMPTY = 1,
};


// Generic deduplicator. Throw in a block of memory, get a reference ID back.
// Any duplicate returns the same ref.
class Dedup
{
public:
    Dedup(GC& gc, bool extrabyte);
    ~Dedup();
    bool init();
    void dealloc();

    // Copies and stores internally. Appends an extra 0 byte to the copy if specified in the ctor.
    sref putCopy(const void *mem, size_t bytes);

    // Deletes memory if it already exists. If extrabyte is set, assumes that the passed in mem is actually bytes+1 size.
    sref putTakeOver(void *mem, size_t bytes, size_t actualsize);

    MemBlock get(sref ref) const;

    sref find(const void *mem, size_t bytes) const;

    // for the GC
    void mark(sref ref);
    tsize sweepstep(tsize step); // if true, one sweep is completed. call sweepfinish() afterward. pass 0 for a complete collection, >0 for this many deletions
    void sweepfinish(bool compact); // resets GC back to the start. Compacting uses some more time and is not necessary for a quick collection

    struct HBlock
    {
        MemBlock mb;          // ^
        uhash h;              // |-- All of this (sizeof(HBlock)-1) is used to store a short block
        unsigned char pad[2]; // v
        unsigned char extraBytesToFree;
        unsigned char x;      // extra bits, 0 if ready to be kicked during _compact()
    };

    enum
    {
        MAX_SHORT_LEN = sizeof(HBlock) - 1,
        SHRT_SIZE_MASK = 0x3f,
        LONG_BIT = 0x40,
        MARK_BIT = 0x80,
    };

private:
    struct HKey
    {
        uhash h;
        tsize ref; // if 0, it's a free slot. Never 1. Indexes into arr[].
    };

    HKey *_prepkey(const void *mem, size_t bytes);
    HBlock *_prepblock();
    bool _acopy(HBlock& dst, const void *mem, size_t bytes); // allocs 1 byte more to make sure it always ends with a 0 byte
    void _atakeover(HBlock& dst, void *mem, size_t bytes, size_t actualsize);
    void _afree(void *mem, size_t bytes);
    HKey *_kresize(tsize newsize);
    void _regenkeys(HKey *ks, tsize newmask);
    void _compact();
    void _recycle(HBlock& hb);
    tsize _indexof(const HBlock& hb) const;
    sref _finishset(HBlock& hb, HKey& k);
    HKey *keys;
    tsize mask; // capacity of keys[] = mask + 1
    PodArray<HBlock> arr; // arr[0], arr[1] are sentinels and store an empty and a zero-sized MemBlock, respectively

    const tsize _extrabyte;
    uhash _hashseed;
    tsize _nextfree; // no unused freelist element if this is < 2 (it's either 0 or >= 2)
    // for the GC
    tsize _sweeppos;
    tsize _inuse;
public:
    GC& gc;
};
