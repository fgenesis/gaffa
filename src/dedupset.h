#pragma once

#include "defs.h"
#include "array.h"

class GC;

struct MemBlock
{
    const char *p;
    size_t n;
};


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
    sref putTakeOver(void *mem, size_t bytes);

    MemBlock get(sref ref) const;

    // for the GC
    void mark(sref ref);
    bool sweepstep(tsize step); // if true, one sweep is completed. call sweepfinish() afterward. pass 0 for a complete collection, >0 for this many deletions
    void sweepfinish();

    struct HBlock
    {
        MemBlock mb;          // ^
        uhash h;              // |-- All of this (sizeof(HBlock)-1) is used to store a short block
        unsigned char pad[3]; // v
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
    bool _acopy(HBlock& dst, const void *mem, size_t bytes); // allocs 1 byte more to make sure it always ends with a 0 byte
    void _afree(void *mem, size_t bytes);
    HKey *_kresize(tsize newsize);
    void _regenkeys(HKey *ks, tsize newmask);
    void _compact();
    HKey *keys;
    tsize mask; // capacity of keys[] = mask + 1
    PodArray<HBlock> arr; // arr[0], arr[1] are sentinels and store an empty and a zero-sized MemBlock, respectively
    GC& _gc;
    const tsize _extrabyte;
    uhash _hashseed;
    tsize _sweeppos;
    tsize _inuse;
};
