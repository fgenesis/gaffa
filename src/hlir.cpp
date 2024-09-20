#include "hlir.h"
#include <string.h>

#include "symstore.h"


HLIRBuilder::HLIRBuilder(GC& gc)
    : gc(gc), b(NULL)
{

}

HLIRBuilder::~HLIRBuilder()
{
    clear();
}

HLNode* HLIRBuilder::alloc()
{
    for(;;)
    {
        if(b)
        {
            HLNode *p = b->alloc();
            if(p)
            {
                memset(p, 0, sizeof(*p));
                return p;
            }
        }
        Block *newb = allocBlock(b ? b->cap * 2 : 16 * sizeof(HLNode));
        if(newb)
        {
            newb->prev = b;
            b = newb;
        }
        else
            break;
    }
    return NULL;
}

void HLIRBuilder::clear()
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

HLIRBuilder::Block* HLIRBuilder::allocBlock(size_t sz)
{
    sz += sizeof(Block);
    Block *newb = gc_alloc_unmanaged_T<Block>(gc, NULL, 0, sz);
    if(newb)
    {
        newb->prev = NULL;
        newb->used = sizeof(Block);
        newb->cap = sz;
    }
    return newb;
}

HLNode* HLIRBuilder::Block::alloc()
{
    size_t avail = cap - used;
    if(avail < sizeof(HLNode))
        return NULL;
    void *p = (char*)this + used;
    used += sizeof(HLNode);
    return (HLNode*)p;
}

HLNode *HLList::add(HLNode* node, GC& gc)
{
    if(used == cap)
    {
        const size_t newcap = 4 + (2 * cap);

        HLNode **newlist = gc_alloc_unmanaged_T<HLNode*>(gc, list, cap, newcap);
        if(!newlist)
            return NULL;
        list = newlist;
        cap = newcap;
    }

    list[used++] = node;
    return node;
}
