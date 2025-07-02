#pragma once

#include "typing.h"

class GC;

class BlockListAllocator
{
public:
    BlockListAllocator(GC& gc);
    ~BlockListAllocator();

    void *alloc(size_t bytes);
    void clear();

    GC& gc;

private:

	struct Block
    {
        void *alloc(size_t bytes);

        Block *prev;
        size_t used;
        size_t cap;
        // payload follows

    };

    Block *allocBlock(size_t sz);
    Block *b;
};
