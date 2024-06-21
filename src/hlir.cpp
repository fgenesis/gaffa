#include "hlir.h"
#include <string.h>


HLIRBuilder::HLIRBuilder(const GaAlloc& ga)
    : galloc(ga), b(NULL)
{

}

HLIRBuilder::~HLIRBuilder()
{
    clear();
}

HLNode* HLIRBuilder::alloc(HLNodeType ty)
{
    for(;;)
    {
        if(b)
        {
            HLNode *p = b->alloc();
            if(p)
            {
                p->type = ty;
                memset(&p->u, 0, sizeof(p->u));
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
        galloc.alloc(galloc.ud, b, b->cap, 0);
        b = prev;
    }
    this->b = NULL;
}

HLIRBuilder::Block* HLIRBuilder::allocBlock(size_t sz)
{
    sz += sizeof(Block);
    Block *newb = (Block*)galloc.alloc(galloc.ud, NULL, 0, sz);
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

HLNode *HLList::add(HLNode* node, const GaAlloc& ga)
{
    if(used == cap)
    {
        const size_t newcap = 4 + (2 * cap);
        HLNode **newlist = (HLNode**)ga.alloc(ga.ud, list, cap * sizeof(HLNode*), newcap * sizeof(HLNode*));
        if(!newlist)
            return NULL;
        list = newlist;
        cap = newcap;
    }

    list[used++] = node;
    return node;
}
