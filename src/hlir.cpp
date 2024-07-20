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

static inline void invalidate(HLNode *n)
{
    n->type = HLNODE_NONE;
}

static void fix0(HLNode *n)
{
    switch(n->type)
    {
        // -2, +2 is how to make explicit signed literals
        case HLNODE_UNARY:
            if(n->tok == Lexer::TOK_PLUS || n->tok == Lexer::TOK_MINUS)
            {
                HLNode *rhs = n->u.unary.rhs;
                if(rhs->type == HLNODE_CONSTANT_VALUE && rhs->u.constant.val.type == PRIMTYPE_UINT)
                {
                    *n = *rhs;
                    invalidate(rhs);
                    n->u.constant.val.type = PRIMTYPE_SINT;
                    return;
                }
            }
            break;

    }
}


void hlirPass0(HLNode *root)
{
    if(!root || root->type == HLNODE_NONE)
        return;

    fix0(root);

    unsigned N = root->_nch;
    HLNode **ch = &root->u.aslist[0];
    if(N == HLList::Children)
    {
        ch = root->u.list.list;
        N = root->u.list.used;
    }

    for(unsigned i = 0; i < N; ++i)
        hlirPass0(ch[i]);
}
