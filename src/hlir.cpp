#include "hlir.h"
#include <string.h>

#include "symstore.h"
#include "gaobj.h"
#include "table.h"


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
    return list->used && list->list[list->used - 1]->flags & HLFLAG_VARIADIC;
}

static int variadicsize(const HLList *list)
{
    int n = (int)list->used;
    if(isvariadic(list))
        n = -n; // Note that the last list element is the variadic indicator element;
    return n;   // to get the size without it, use abs(n) - (n < 0)
}


int HLFunctionHdr::nargs() const
{
    int n = 0;
    if(paramlist)
    {
        const HLList *list = paramlist->as<HLList>();
        n = variadicsize(list);
    }
    return n;
}

int HLFunctionHdr::nrets() const
{
    int n = 0;
    if(rettypes)
    {
        const HLList *list = rettypes->as<HLList>();
        n = variadicsize(list);
    }
    return n;
}


// This is the entry point of the tree folding. MUST start at a function node.
// THis folds recursively until hitting another function.
// This function is then packaged as a prototype and not further folded.
HLNode *HLNode::fold(HLFoldTracker& ft) const
{
    assert(type == HLNODE_FUNCTION);

    HLNode *cp = clone(ft.gc);


    HLFunctionHdr *h = u.func.hdr->as<HLFunctionHdr>();

    Type t = { PRIMTYPE_ANY };
    Table params(t, t);

    // These are NULL if there are no parameters or return values specified
    HLList *paramlist = h->paramlist ? h->paramlist->aslist(HLNODE_LIST) : NULL;
    HLList *retlist = h->rettypes ? h->rettypes->aslist(HLNODE_LIST) : NULL;

    HLList *body = u.func.body->aslist(HLNODE_BLOCK);


    assert(false); // TODO

    return NULL;
}


HLFoldResult HLNode::_foldRec(HLFoldTracker& ft)
{
    if(_nch)
    {
        const size_t N = numchildren();
        HLNode **ch = children();
        for(size_t i = 0; i < N; ++i)
            if(ch[i])
                ch[i]->fold(ft);
    }

    switch(type)
    {
        default:
            break;

        case HLNODE_IDENT:
            if(flags & IDENTFLAGS_RHS)
            {
                const Symstore::Sym *sym = ft.syms.getsym(u.ident.symid);
                if(const Val *v = sym->value()) // Symbol has a known value? Become that value.
                    return makeconst(ft.gc, *v);
            }
            break;

        case HLNODE_VARDECLASSIGN:
        {
            // This may be the case with deferred decl; that has no values
            if(!u.vardecllist.vallist)
                break;

            const HLList& decls = *u.vardecllist.decllist->as<HLList>();
            const HLList& vals = *u.vardecllist.vallist->as<HLList>();
            const size_t N = std::min(decls.used, vals.used);
            HLNode * const *pd = decls.list;
            HLNode * const *pv = vals.list;
            // FIXME: this might be incorrect for functions with variadic returns, check this later
            // HMM: might have to introduce a HLConstantValueList to store multiple values
            // for example if a function can be entirely evaluated at compile time
            size_t i = 0;
            bool all = true;
            for( ; i < N; ++i)
            {
                HLNode * const lhs = pd[i];
                HLNode * const rhs = pv[i];
                if(lhs->type == HLNODE_VARDEF)
                {
                    const HLVarDef& vd = *lhs->as<HLVarDef>();
                    const HLNode * const target = vd.ident;
                    const HLNode * const ttype = vd.type;

                    // Propagate if assigning a known value to an identifier
                    // TODO TYPE CHECKS
                    if(rhs->isconst() && target->type == HLNODE_IDENT && target->flags & IDENTFLAGS_LHS)
                    {
                        Symstore::Sym *sym = ft.syms.getsym(target->u.ident.symid);
                        if(!sym->isMutable())
                        {
                            sym->setValue(rhs->as<HLConstantValue>()->val);
                            lhs->clear(ft.gc); // These are removed during child compaction
                            rhs->clear(ft.gc);
                            continue;
                        }
                    }
                }

                all = false;

                if(rhs->iscall()) // Calls may have variadic returns and we lose what goes where, stop
                    break;
            }
            if(all)
            {
                clear(ft.gc);

            }
        }

        case HLNODE_FUNCTION:
            return _foldfunc(ft);

        case HLNODE_FUNCDECL:
        {
            HLIdent *ident = u.funcdecl.ident->as<HLIdent>();
            HLIdent *ns = u.funcdecl.namespac->as<HLIdent>();
            // This was already folded from HLFunction
            HLConstantValue *func = u.funcdecl.value->as<HLConstantValue>();
        }
    }

    // Compact children if we have any
    if(_nch)
    {
        const size_t N = numchildren();
        HLNode **ch = children();

        // Remove HLNone nodes
        for(size_t i = 0; i < N; ++i)
            if(ch[i] && ch[i]->type == HLNODE_NONE)
                ch[i] = NULL;

        // Compact lists that have NULL elements
        // Important to leave the regular 0..3 children intact and not shift those up,
        // as these are aliased to struct members
        if(_nch == HLList::Children)
        {
            size_t w = 0;
            for(size_t i = 0; i < N; ++i)
                if(ch[i])
                    ch[w++] = ch[i];
            u.list.used = w;
        }
    }

    return FOLD_OK;
}

HLFoldResult HLNode::makeconst(GC& gc, const Val& val)
{
    HLNode *me = morph<HLConstantValue>(gc);
    me->u.constant.val = val;
    return FOLD_OK;
}

void HLNode::clear(GC& gc)
{
    if(const size_t nch = numchildren())
    {
        HLNode **ch = children();
        for(size_t i = 0; i < nch; ++i)
            ch[i]->clear(gc);
    }

    if(!(flags & HLFLAG_NOEXTMEM))
    {
        if(_nch == HLList::Children)
            gc_alloc_unmanaged_T<HLNode*>(gc, u.list.list, u.list.cap, 0);
    }

    _nch = 0;
    type = HLNODE_NONE;
}

HLFoldResult HLNode::_foldfunc(HLFoldTracker& ft)
{
    HLFoldResult fr = _tryfoldfunc(ft);
    if(fr == FOLD_LATER)
    {
        assert(false); // TODO: emit as proto
        fr = FOLD_OK;
    }

    return fr;
}


HLFoldResult HLNode::_tryfoldfunc(HLFoldTracker& ft)
{
    assert(type == HLNODE_FUNCTION);

    HLFunctionHdr *h = u.func.hdr->as<HLFunctionHdr>();

    Type t = { PRIMTYPE_ANY };
    Table params(t, t);

    // These are NULL if there are no parameters or return values specified
    HLList *paramlist = h->paramlist ? h->paramlist->aslist(HLNODE_LIST) : NULL;
    HLList *retlist = h->rettypes ? h->rettypes->aslist(HLNODE_LIST) : NULL;

    HLList *body = u.func.body->aslist(HLNODE_BLOCK);
    // Precondition: Anything in body has been folded already

    // TODO: support default parameters at some point?
    // StructMember is already equipped for this.
    // Also, named params?

    FuncInfo info = {};

    // Fold params
    Type paramt = {};
    if(paramlist)
    {
        StructMember sm[256];
        assert(paramlist->used < Countof(sm));

        const size_t n = paramlist->used;
        for(size_t i = 0; i < n; ++i)
        {
            StructMember& m = sm[i];
            const HLVarDef *vd = paramlist->list[i]->as<HLVarDef>();
            m.defaultval = _Xnil();
            m.name = vd->ident->as<HLIdent>()->nameStrId;
            const Val& thetype = vd->type->as<HLConstantValue>()->val;
            m.t = thetype.u.t->tid;
            assert(m.t.id == PRIMTYPE_TYPE);
        }

        // TODO: variadic?

        info.nargs = n;
        paramt = ft.tr.mkstruct(&sm[0], n, 0);
    }


    // TODO: autodetect return types

    // Fold return types
    Type rett = {};
    if(retlist)
    {
        StructMember sm[256];
        assert(retlist->used < Countof(sm));
        const size_t n = retlist->used;
        for(size_t i = 0; i < n; ++i)
        {
            StructMember& m = sm[i];
            m.defaultval = _Xnil();
            m.name = 0;
            const Val& thetype = retlist->list[i]->as<HLConstantValue>()->val;
            m.t = thetype.u.t->tid;
            assert(m.t.id == PRIMTYPE_TYPE);
        }

        // TODO: variadic?

        info.nrets = n;
        rett = ft.tr.mkstruct(&sm[0], n, 0);
    }

    const Type subs[] = { paramt, rett };
    info.t = ft.tr.mksub(PRIMTYPE_FUNC, &subs[0], Countof(subs));



    DFunc *f = (DFunc*)gc_new(ft.gc, sizeof(DFunc), PRIMTYPE_FUNC);

    f->info = info;

    //f->u.gfunc.vmcode = ...
    // TODO: populate func body

    ValU v;
    v.u.obj = f;
    v.u.t = ft.tr.lookup(info.t)->dtype;
    assert(v.u.t);

    return makeconst(ft.gc, v);
}

size_t HLNode::memoryNeeded() const
{
    size_t bytes = sizeof(*this);

    size_t nch = _nch;
    const HLNode * const *ch = &u.aslist[0];
    if(nch == HLList::Children)
    {
        nch = u.list.used;
        bytes += nch * sizeof(HLNode*);
        ch = u.list.list;
    }

    for(size_t i = 0; i < nch; ++i)
        if(const HLNode *c = ch[i])
            if(c->type != HLNODE_FUNCTION) // Don't cross function boundary
                bytes += c->memoryNeeded();

    return bytes;
}

// Clone one function into its own
HLNode * HLNode::clone(GC & gc) const
{
    assert(type == HLNODE_FUNCTION); // There's little reason to call this for anything else
    const size_t bytes = memoryNeeded();
    printf("DEBUG: Cloning function in line %u using %u bytes\n", this->line, (unsigned)bytes);
    void *mem = gc_alloc_unmanaged(gc, NULL, 0, bytes);
    HLNode *cp = _clone(mem, bytes, gc);
    cp->as<HLFunction>()->clonedMemSize = bytes;
    return cp;
}

HLNode* HLNode::_clone(void* mem, size_t bytes, GC& gc) const
{
    assert(_nch <= Countof(u.aslist)); // This is only called on functions, which are no lists
    byte * const begin = (byte*)mem;
    HLNode * const target = (HLNode*)begin;
    byte *end = this->_cloneRec(begin + sizeof(*this), target, gc);
    assert(end - begin == bytes);
    return target;
}

byte *HLNode::_cloneRec(byte *m, HLNode *target, GC& gc) const
{
    memcpy(target, this, sizeof(*this));

    size_t nch = _nch;
    HLNode **dst = &target->u.aslist[0];
    const HLNode * const *src = &u.aslist[0];
    if(nch == HLList::Children)
    {
        src = u.list.list;
        nch = u.list.used;
        dst = (HLNode**)m; m += nch * sizeof(HLNode*); // Allocate space for the child pointer list
        target->u.list.list = dst;
        target->u.list.used = nch;
        target->u.list.cap = nch;
        target->flags |= HLFLAG_NOEXTMEM;
    }

    // Check how many children we'll actually own. Functions allocate on their own and are not part of our memory block.
    size_t ownch = 0;
    for(size_t i = 0; i < nch; ++i)
        if(const HLNode *s = src[i])
            ownch += s->type != HLNODE_FUNCTION;

    // Allocate space for child nodes
    HLNode *ch = (HLNode*)m; m += ownch * sizeof(HLNode);

    for(size_t i = 0; i < nch; ++i)
    {
        const HLNode *s = src[i];
        HLNode *d = NULL;
        if(s)
        {
            if(s->type != HLNODE_FUNCTION)
            {
                d = &ch[i];
                m = s->_cloneRec(m, d, gc); // Advances in our block
            }
            else
            {
                d = s->clone(gc); // Allocates its own memory
            }
        }
        dst[i] = d;
    }

    return m;
}

