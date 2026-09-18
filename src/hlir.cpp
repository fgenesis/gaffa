#include "hlir.h"
#include <string.h>
#include <sstream>

#include "symstore.h"
#include "gaobj.h"
#include "table.h"
#include "runtime.h"


HLIRBuilder::HLIRBuilder(GC& gc)
    : bla(gc)
{
}

HLIRBuilder::~HLIRBuilder()
{
}

HLNode* HLIRBuilder::list(GC& gc, size_t prealloc)
{
    HLNode *ls = this->list();
    return ls->u.list.resize(gc, prealloc) ? ls : NULL;
}

HLNode *HLList::add(HLNode* node, GC& gc)
{
    if(used == cap)
    {
        const size_t newcap = 4 + (2 * cap);
        if(!resize(gc, newcap))
            return NULL;
    }

    list[used++] = node;
    return node;
}

HLNode **HLList::resize(GC& gc, size_t n)
{
    HLNode **newlist = gc_alloc_unmanaged_T<HLNode*>(gc, list, cap, n);
    if(newlist)
    {
        list = newlist;
        cap = n;
    }
    return newlist;
}

HLNode::~HLNode()
{
    assert(type == HLNODE_NONE);
}

bool HLNode::isconst() const
{
    return type == HLNODE_CONSTANT_VALUE;
}

bool HLNode::iscall() const
{
    return type == HLNODE_CALL || type == HLNODE_MTHCALL;
}

// Whether a func arg list or return type list is variadic
static bool isvariadic(const HLList *list)
{
    // Check if the last list element has the variadic flag set
    return list->used && (list->list[list->used - 1]->flags & HLFLAG_VARIADIC);
}

static HLFunctionHdr::Values variadicsize(const HLList *list)
{
    HLFunctionHdr::Values ret;
    ret.variadic = isvariadic(list);
    // Note that the last list element is the variadic indicator element, so the actual number is one less
    ret.n = list->used - ret.variadic;
    return ret;
}


HLFunctionHdr::Values HLFunctionHdr::nargs() const
{
    Values ret = {};
    if(paramlist)
    {
        const HLList *list = paramlist->as<HLList>();
        ret = variadicsize(list);
    }
    return ret;
}

HLFunctionHdr::Values HLFunctionHdr::nrets() const
{
    Values ret = {};
    if(rettypes)
    {
        const HLList *list = rettypes->as<HLList>();
        ret = variadicsize(list);
    }
    return ret;
}

