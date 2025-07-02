#include "gaalloc.h"
#include "gc.h"
#include <string.h>


BlockListAllocator::BlockListAllocator(GC & gc)
    : gc(gc), b(NULL)
{
}

BlockListAllocator::~BlockListAllocator()
{
	clear();
}

void* BlockListAllocator::alloc(size_t bytes)
{
    Block *b = this->b;
    for(;;)
    {
        if(b)
            if(void *p = b->alloc(bytes))
                return memset(p, 0, bytes);

        const size_t s1 = b ? b->cap * 2 : 0;
        const size_t s2 = 16 * bytes;
        b = allocBlock(s1 > s2 ? s1 : s2);
        if(!b)
            break;

        b->prev = this->b;
        this->b = b;
    }
    return NULL;
}

void BlockListAllocator::clear()
{
    Block *b = this->b;
    while(b)
    {
        Block * const prev = b->prev;
        gc_alloc_unmanaged(gc, b, b->cap, 0);
        b = prev;
    }
    this->b = NULL;
}

BlockListAllocator::Block* BlockListAllocator::allocBlock(size_t sz)
{
    sz += sizeof(Block);
    Block *newb = gc_alloc_unmanaged_T<Block>(gc, NULL, 0, sz);
    if(newb)
    {
        newb->prev = NULL;
        newb->used = sizeof(Block); // don't touch the header
        newb->cap = sz;
    }
    return newb;
}

void* BlockListAllocator::Block::alloc(size_t bytes)
{
    size_t avail = cap - used;
    if(avail < bytes)
        return NULL;
    void *p = (char*)this + used;
    used += bytes;
    return p;
}
