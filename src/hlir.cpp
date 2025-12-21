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
HLNode *HLNode::fold(HLFoldTracker& ft, HLFoldStep step)
{
    assert(type == HLNODE_FUNCTION);

    // Recursively fold everything as a first step so that anything that can be eliminated
    // is already  eliminated before the next steps start
    _foldRec(ft, step);

    // Chop functions into separate memory blocks so that they can be specialized individually
    HLNode *cp = clone(ft.vm.rt->gc);

    return cp;
}


HLFoldResult HLNode::_foldRec(HLFoldTracker& ft, HLFoldStep step)
{
    if(_nch)
    {
        const size_t N = numchildren();
        HLNode **ch = children();
        for(size_t i = 0; i < N; ++i)
            if(ch[i])
                ch[i]->_foldRec(ft, step);
    }

    switch(type)
    {
        default:
            break;

        case HLNODE_CONSTANT_VALUE:
            setknowntype(u.constant.val.type);
            break;


        case HLNODE_IDENT:
            if(flags & IDENTFLAGS_RHS)
            {
                const Symstore::Sym *sym = ft.syms.getsym(u.ident.symid);
                if(const Val *v = sym->value()) // Symbol has a known value? Become that value.
                    return makeconst(ft.vm.rt->gc, *v);
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
            // -> no, HLList of HLConstantValue is fine
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
                            lhs->clear(ft.vm.rt->gc); // These are removed during child compaction
                            rhs->clear(ft.vm.rt->gc);
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
                clear(ft.vm.rt->gc);
            }
        }
        break;

        case HLNODE_BINARY:
            _foldBinop(ft);
            break;

        case HLNODE_UNARY:
            _foldUnop(ft);
            break;

        case HLNODE_RETURNYIELD:
            _applyTypeFromList(ft, u.retn.what);
            break;


        /*case HLNODE_FUNCTION:
            return _foldfunc(ft);

        case HLNODE_FUNCDECL:
        {
            HLIdent *ident = u.funcdecl.ident->as<HLIdent>();
            HLIdent *ns = u.funcdecl.namespac->as<HLIdent>();
            // This was already folded from HLFunction
            HLConstantValue *func = u.funcdecl.value->as<HLConstantValue>();
        }*/
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

void HLNode::setknowntype(sref tid)
{
    assert(!(flags & HLFLAG_KNOWNTYPE));
    flags |= HLFLAG_KNOWNTYPE;
    mytype = tid;
}

HLFoldResult HLNode::makeconst(GC& gc, const Val& val)
{
    HLNode *me = morph<HLConstantValue>(gc);
    me->u.constant.val = val;
    me->setknowntype(val.type);
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
            m.t = thetype.asDType()->tid;
            assert(m.t == PRIMTYPE_TYPE);
        }

        // TODO: variadic?

        info.nargs = n;
        info.paramtype = ft.vm.rt->tr.mkstruct(&sm[0], n, 0);
    }


    // TODO: autodetect return types

    // Fold return types
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
            m.t = thetype.asDType()->tid;
            assert(m.t == PRIMTYPE_TYPE);
        }

        // TODO: variadic?

        info.nrets = n;
        info.rettype = ft.vm.rt->tr.mkstruct(&sm[0], n, 0);
    }

    const Type subs[] = { info.paramtype, info.rettype };
    info.functype = ft.vm.rt->tr.mksub(PRIMTYPE_FUNC, &subs[0], Countof(subs));



    DFunc *f = (DFunc*)gc_new(ft.vm.rt->gc, sizeof(DFunc), PRIMTYPE_FUNC);

    f->info = info;

    //f->u.gfunc.vmcode = ...
    // TODO: populate func body


    return makeconst(ft.vm.rt->gc, Val(f));
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
            if(c->type != HLNODE_FUNCTION && c->type != HLNODE_FUNC_PROTO) // Don't cross function boundary
                bytes += c->memoryNeeded();

    return bytes;
}

// Clone one function into its own memory block.
HLNode * HLNode::clone(GC & gc) const
{
    assert(type == HLNODE_FUNCTION); // There's little reason to call this for anything else
    const size_t sizeForSubtree = memoryNeeded();
    const size_t totalbytes = sizeForSubtree + sizeof(FuncProto) + sizeof(HLNode);
    printf("DEBUG: Cloning function in line %u using %u bytes\n", this->line, (unsigned)totalbytes);
    byte *mem = (byte*)gc_alloc_unmanaged(gc, NULL, 0, totalbytes);

    HLNode *funcroot = (HLNode*)mem;
    memcpy(funcroot, this, sizeof(HLNode)); // Copy metadata like line number; this copies some more but whatev
    mem += sizeof(*funcroot);

    FuncProto *proto = (FuncProto*)mem;
    mem += sizeof(*proto);

    funcroot->unsafemorph<HLFuncProto>();
    funcroot->u.funcproto.proto = proto;

    proto->refcount = 1;
    proto->memsize = totalbytes;
    proto->node = _clone(mem, sizeForSubtree, gc);

    return funcroot;
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
            switch(s->type)
            {
                case HLNODE_FUNC_PROTO:
                    s->u.funcproto.proto->refcount++; // Don't clone already packaged functions
                    // fall through
                default:
                    d = &ch[i];
                    m = s->_cloneRec(m, d, gc); // Advances in our block
                    break;

                case HLNODE_FUNCTION:
                    d = s->clone(gc); // Allocates its own memory
                    break;
            }
        }
        dst[i] = d;
    }

    return m;
}



HLFoldResult HLNode::_foldUnop(HLFoldTracker& ft)
{
    return HLFoldResult();
}

#if 0
DType* HLNode::getDType(HLFoldTracker& ft)
{
    if(_nch == HLList::Children)
    {
        StructMember sm[256];
        assert(used < Countof(sm));
        const size_t n = used;
        for(size_t i = 0; i < n; ++i)
        {
            StructMember& m = sm[i];
            m.defaultval = _Xnil();
            m.name = 0;
            const Val& thetype = u.list[i]->as<HLConstantValue>()->val;
            m.t = thetype.u.t->tid;
            assert(m.t.id == PRIMTYPE_TYPE);
        }

        // TODO: variadic?

        info.nrets = n;
        rett = ft.tr.mkstruct(&sm[0], n, 0);
    }
    else
    {
        Type t = { mytype };
        TDesc *desc = ft.tr.lookup(t);
        return desc->dtype;
    }

}
#endif

void HLNode::_applyTypeFrom(HLFoldTracker& ft, HLNode* from)
{
    if(from->isknowntype())
        propagateMyType(ft, from);
}

void HLNode::_applyTypeFromList(HLFoldTracker& ft, HLNode* from)
{
    assert(from->_nch == HLList::Children);
    if(from->isknowntype())
    {
        propagateMyType(ft, from);
    }
}

// Sets my own type and propagates to children
HLFoldResult HLNode::propagateMyType(HLFoldTracker& ft, const HLNode *typesrc)
{
    assert(typesrc->isknowntype());

    if(this->isknowntype())
    {
        // FIXME: This needs to be a proper subtype check
        if(typesrc->mytype == mytype)
        {
            return FOLD_OK;
        }
        else
        {
            std::ostringstream os;
            // TODO: get source location if possible, and show it
            const char *toktxt = Lexer::GetTokenText(Lexer::TokenType(this->tok));
            os << "(" << line << ":" << column << ") '" << toktxt << "': mismatched types, found " << mytype << ", expected " << typesrc->mytype;
            ft.errors.push_back(os.str());
            puts(os.str().c_str());
            return FOLD_FAILED;
        }
    }

    this->setknowntype(typesrc->mytype);

    HLFoldResult res = FOLD_OK;

    switch(type)
    {
        case HLNODE_UNARY:
            _foldUnop(ft);
            break;

        case HLNODE_BINARY:
            _foldBinop(ft);
            break;

        default:
            assert(false);

    }

    size_t numch = 0;

    // Propagate to children
    switch(type)
    {
        case HLNODE_UNARY:
        case HLNODE_BINARY:
            numch = _nch;
            break;

        default:
            assert(false);

    }

    for(size_t i = 0; i < numch; ++i)
    {
        res = u.aslist[i]->propagateMyType(ft, typesrc);
        if(res != FOLD_OK)
            return res;
    }

    return FOLD_OK;
}

void HLNode::_deductMyType(HLFoldTracker &ft)
{
    switch((HLNodeType)type)
    {
        // TODO: big switch over all the things
    }
}

void HLNode::_propagateTypeFromChildren(HLFoldTracker &ft)
{
    // Do children first -- this goes into the leaves...
    HLNode **children = this->children();
    size_t nch = this->numchildren();
    for(size_t i = 0; i < nch; ++i)
        children[i]->_propagateTypeFromChildren(ft);

    // ... then work back up to the root (upon return)
    _deductMyType(ft);
}

void HLNode::_propagateTypeToChildren(HLFoldTracker &ft)
{
    assert(mytype != PRIMTYPE_AUTO);
    HLNode **children = this->children();
    size_t nch = this->numchildren();
    for(size_t i = 0; i < nch; ++i)
        children[i]->_inheritType(ft, this);
}

void HLNode::_inheritType(HLFoldTracker &ft, HLNode* from)
{
    if(this->isknowntype())
    {
        // FIXME: This needs to be a proper subtype check
        if(from->mytype == mytype)
        {
            // All good
        }
        else
        {
            std::ostringstream os;
            // TODO: get source location if possible, and show it
            const char *toktxt = Lexer::GetTokenText(Lexer::TokenType(this->tok));
            os << "(" << line << ":" << column << ") '" << toktxt << "': mismatched types, found " << mytype << ", expected " << from->mytype;
            ft.errors.push_back(os.str());
            puts(os.str().c_str());
        }
    }
    else
    {
        setknowntype(from->mytype);
        _propagateTypeToChildren(ft);
    }

}

HLFoldResult HLNode::_foldBinop(HLFoldTracker& ft)
{
    assert(type == HLNODE_BINARY);

    HLNode *L = u.binary.a;
    HLNode *R = u.binary.b;
    const Lexer::TokenType tt = Lexer::TokenType(tok);
    const char *opname = Lexer::GetTokenText(tt);
    Str name = ft.vm.rt->sp.put(opname);

    GC &gc = ft.vm.rt->gc;

    Type ns = L->mytype;
    if(ns == PRIMTYPE_AUTO)
    {
        ns = PRIMTYPE_ANY;
        std::ostringstream os;
        os << "unknown argument type for operator '" << opname << "', assuming 'any'";
        ft.warn(this, os.str().c_str());
    }

    const Val *opr = ft.env.lookupInNamespace(ns, name.id);
    if(!opr)
    {
        std::ostringstream os;
        os << "type has no operator '" << opname << "'";
        ft.error(this, os.str().c_str());
        return FOLD_FAILED;
    }
    const DFunc *fopr = opr->asFunc();
    if(!fopr)
    {
        std::ostringstream os;
        os << "type's '" << opname << "' is not a function";
        ft.error(this, os.str().c_str());
        return FOLD_FAILED;
    }

    if(L->isconst() && R->isconst() && fopr->isPure())
    {
        Val stk[] = { L->u.constant.val, R->u.constant.val };
        fopr->call(&ft.vm, stk);
        makeconst(gc, stk[0]);
        return FOLD_OK;
    }

    HLNode *params = ft.hlir.list(); // TOOD: prealloc, known to be 2 elems
    params->u.list.add(L, gc);
    params->u.list.add(R, gc);

    HLNode *me = morph<HLResolvedCall>(gc);
    me->u.resolvedcall.paramlist = params;
    me->u.resolvedcall.func = fopr;
    me->setknowntype(fopr->info.rettype);

    return FOLD_OK;
}

void HLFoldTracker::error(const HLNode* where, const char *msg)
{
    printf("(%s:%u): %s\n", filename.c_str(), where->line, msg);
}

void HLFoldTracker::warn(const HLNode* where, const char *msg)
{
    printf("(%s:%u): Warning: %s\n", filename.c_str(), where->line, msg);
}
