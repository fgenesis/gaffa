#include "hlir.h"
#include <string.h>

#include "symstore.h"


HLIRBuilder::HLIRBuilder(GC& gc)
    : bla(gc)
{
}

HLIRBuilder::~HLIRBuilder()
{
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
