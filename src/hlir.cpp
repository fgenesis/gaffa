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


typedef Type (*TypeExtractor)(HLFoldTracker& ft, const HLNode *node);

static Type nodeValueAsType(HLFoldTracker& ft, const HLNode *node)
{
    if(!node->isconst())
    {
        ft.error(node, "Expected type constant");
        return PRIMTYPE_NIL;
    }

    Val v = node->as<HLConstantValue>()->val;
    if(v.type != PRIMTYPE_TYPE)
    {
        ft.error(node, "Expected type");
        return PRIMTYPE_NIL;
    }

    return v.asDType()->tid;
}

static Type nodeKnownType(HLFoldTracker& ft, const HLNode *node)
{
    if(node->mytype == PRIMTYPE_NIL)
    {
        ft.error(node, "Attempt to get type of typeless expression");
        return PRIMTYPE_NIL;
    }

    if(node->isknowntype())
        return node->mytype;

    ft.warn(node, "Unable to deduce type, assuming any");
    return PRIMTYPE_ANY;
}

static Type typeOfVarDef(HLFoldTracker& ft, const HLNode *node)
{
    return nodeValueAsType(ft, node->as<HLVarDef>()->type);
}


// Generates a list type from a HLList containing nodes of types
// for example: func f(...) -> int, float, string
// intended for function params and return values
// if you have more than 256 entries actually written out you're doing it wrong
static Type typelistFromTypes(HLFoldTracker& ft, const HLNode *node, TypeExtractor extr)
{
    Type ts[256];
    size_t n = node->numchildren();
    const HLNode * const *ch = node->children();
    if(n > Countof(ts))
    {
        ft.error(node, "List too long, can't generate type");
        return PRIMTYPE_NIL;
    }

    for(size_t i = 0; i < n; ++i)
    {
        const HLNode *child = ch[i];
        ts[i] = extr(ft, child);
        if(ts[i] == PRIMTYPE_NIL)
            ft.error(child, "Got unexpected nil entry in type list");
    }

    if(n == 1 && (ts[0] & TYPEBIT_TYPELIST))
        return ts[0];

    return ft.vm.rt->tr.mklist(ts, n);
}


// This is the entry point of the tree folding. MUST start at a function node.
HLNode *HLNode::fold(HLFoldTracker& ft)
{
    assert(type == HLNODE_FUNCTION);

    // Recursively fold everything as a first step so that anything that can be eliminated
    // is already  eliminated before the next steps start
    _foldRec(ft);

    if(!ft.isPackageFunctions())
        return NULL;

    // Chop functions into separate memory blocks so that they can be specialized individually
    //HLNode *cp = clone(ft);
    //return cp;
    return this;
}


void HLNode::_foldRec(HLFoldTracker& ft)
{
    GC& gc = ft.vm.rt->gc;

    if(_nch)
    {
        const size_t N = numchildren();
        HLNode **ch = children();
        for(size_t i = 0; i < N; ++i)
            if(ch[i])
                ch[i]->_foldRec(ft);
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
            //_applyTypeFromList(ft, u.retn.what); // return HAS NO TYPE ON ITS OWN
            if(!u.retn.what->isknowntype())
                u.retn.what->setknowntype(typelistFromTypes(ft, u.retn.what, nodeKnownType));
            break;

        case HLNODE_FUNCTION:
        {
            HLNode *me = this->clone(ft);
            HLFuncProto *fp = me->as<HLFuncProto>();

            DebugInfo *d = gc_alloc_unmanaged_zero_T<DebugInfo>(ft.vm.rt->gc, 1);
            d->linestart = this->line;
            d->lineend = fp->proto->lineend;

            DFunc *df = DFunc::GCNew(ft.vm.rt->gc);
            df->dbg = d;
            df->info = fp->proto->info;
            df->u.proto = fp->proto;

            this->makeconst(gc, Val(df));
        }
        break;

        case HLNODE_FUNCDECL:
        {
            HLIdent *ident = u.funcdecl.ident->as<HLIdent>();
            HLIdent *ns = u.funcdecl.namespac ? u.funcdecl.namespac->as<HLIdent>() : NULL;
            // This was already folded from HLFunction
            Val fv = u.funcdecl.value->as<HLConstantValue>()->val;
            DFunc *df = fv.asFunc();
            const FuncProto *proto = df->u.proto;
            if(df->dbg)
                df->dbg->name = ident->nameStrId;

            assert(!ns && "FIXME");

            Symstore::Sym *sym = ft.syms.getsym(ident->symid);
            assert(!sym->isMutable());

           sym->setValue(Val(df));
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
}

void HLNode::setknowntype(Type tid)
{
    assert(tid != PRIMTYPE_AUTO);
    assert(!(flags & HLFLAG_KNOWNTYPE));
    flags |= HLFLAG_KNOWNTYPE;
    mytype = tid;
}

void HLNode::makeconst(GC& gc, const Val& val)
{
    morph<HLConstantValue>(gc);
    u.constant.val = val;
    setknowntype(val.type);
    tok = Lexer::TOK_SYNTHETIC;
}

void HLNode::clear(GC& gc)
{
    if(const size_t nch = numchildren())
    {
        HLNode **ch = children();
        for(size_t i = 0; i < nch; ++i)
            if(ch[i])
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

static bool hasNulls(const HLNode * const *p, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        if(!p[i])
            return true;
    return false;
}

static VisitResult funcMemSizeVisitor(HLNode *node, void *ud)
{
    // Still need to account for current node even if we don't recurse
    size_t add = sizeof(*node);
    if(node->_nch == HLList::Children)
    {
        // Also store children memory block inline with the rest
        assert(!hasNulls(node->u.list.list, node->u.list.used));
        add += node->u.list.used * sizeof(HLNode*);
    }

    *(size_t*)ud += add;

    switch(node->type)
    {
        case HLNODE_FUNCTION:
        case HLNODE_FUNC_PROTO:
            return VISIT_NOREC;
    }
    return VISIT_CONTINUE;
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

struct ReturnTypeDeductorVisitor
{
    HLFoldTracker& ft;
    HLNode *first;
    Type rettype;
    size_t n;
};

static HLPreVisitResult nodeReturnVisitor(HLNode *node, void *ud)
{
    HLPreVisitResult res = { VISIT_CONTINUE, 0 };
    ReturnTypeDeductorVisitor *vis = (ReturnTypeDeductorVisitor*)ud;

    if(!node || node->type == HLNODE_FUNCTION || node->type == HLNODE_FUNCDECL || node->type == HLNODE_FUNC_PROTO)
    {
        res.res =  VISIT_NOREC;
        return res;
    }

    // TODO: could also use norec for things that are known to be expressions
    // and therefore can't contain a return statement -- probably faster

    if(node->type != HLNODE_RETURNYIELD || node->tok != Lexer::TOK_RETURN)
        return res; // just continue

    // It's a return statement
    assert(node->mytype == PRIMTYPE_NIL); // return is a statement, not an expr

    HLNode *rets = node->u.retn.what;

    ++vis->n;

    if(!vis->first) // First one we find defines the type
        vis->first = rets;

    if(vis->rettype == PRIMTYPE_AUTO)
        vis->rettype = rets->mytype;
    else // Any further ones must have the same type
    {
        if(vis->ft.vm.rt->tr.isListCompatible(rets->mytype, vis->rettype))
        {
            vis->ft.error(rets, "Return statement has mismatching type");
            vis->ft.error(vis->first, "(originally obtained from here)");
        }
    }

    res.res = VISIT_NOREC;
    return res;
}

// Clone one function into its own memory block.
HLNode * HLNode::clone(HLFoldTracker& ft) const
{
    Runtime& rt = *ft.vm.rt;
    assert(type == HLNODE_FUNCTION); // There's little reason to call this for anything else
    const HLFunction * const hf = this->as<HLFunction>();
    const HLFunctionHdr * const hh = hf->hdr->as<HLFunctionHdr>();
    const HLNode * const body = hf->body;

    // We're going to dissect the header later. For now, clone the body.
    // (The header isn't needed later, extract everything needed here)
    // This all goes into one memory block behind the FuncProto.
    const size_t sizeForSubtree = body->memoryNeeded();
    const size_t totalbytes = sizeForSubtree + sizeof(FuncProto) + sizeof(HLNode);
    printf("DEBUG: Cloning function in line %u using %u bytes\n", this->line, (unsigned)totalbytes);
    byte *mem = (byte*)gc_alloc_unmanaged(rt.gc, NULL, 0, totalbytes);

    HLNode *funcroot = (HLNode*)mem;
    memcpy(funcroot, body, sizeof(HLNode)); // Copy metadata like line number; this copies some more but whatev
    mem += sizeof(*funcroot);

    FuncProto * const proto = (FuncProto*)mem;
    mem += sizeof(*proto);

    funcroot->unsafemorph<HLFuncProto>();
    funcroot->u.funcproto.proto = proto;

    proto->refcount = 1;
    proto->memsize = totalbytes;
    proto->lineend = hh->lineend;

    // Clone all local functions first.
    proto->body = body->_clone(mem, sizeForSubtree, ft);

    // Clone done. Now figure out the parameter and return types.

    // These are NULL if there are no parameters or return values specified
    //HLList *paramlist = hh->paramlist ? hh->paramlist->aslist(HLNODE_LIST) : NULL;

    FuncInfo info = {};
    Type paramt = {};
    if(hh->paramlist)
    {
        StructMember sm[256];
        /*
        assert(paramlist->used < Countof(ts));

        const size_t n = paramlist->used;
        for(size_t i = 0; i < n; ++i)
        {
            StructMember& m = sm[i];
            const HLVarDef *vd = paramlist->list[i]->as<HLVarDef>();
            m.defaultval = _Xnil();
            m.name = vd->ident->as<HLIdent>()->nameStrId;
            const Val& thetype = vd->type->as<HLConstantValue>()->val;
            m.t = thetype.asDType()->tid;
            ts[i] = m.t;
            assert(m.t == PRIMTYPE_TYPE);
            assert(false && "wat");
        }
        */

        HLFunctionHdr::Values args = hh->nargs();
        info.nargs = args.n;
        if(args.variadic)
            info.flags |= FuncInfo::VarArgs;
        // TODO make sure last type in the list is variadic

        //info.paramtype = rt.tr.mkstruct(&sm[0], n, 0); // HMM: if we really make a struct here, and it's variadic,
        //   then the last entry must be an Array<T>?
        info.paramtype = typelistFromTypes(ft, hh->rettypes, typeOfVarDef);
    }
    else // No params -- that means we're most likely the file-scope root function, which is f(any?...) aka f(...)
    {
        /*Type varany = PRIMTYPE_ANY | TYPEBIT_OPTIONAL | TYPEBIT_VARIADIC;
        info.paramtype = rt.tr.mklist(&varany, 1); // TODO: Cache this somewhere
        info.flags |= FuncInfo::VarArgs;*/
        info.paramtype = PRIMTYPE_NIL;
        info.nargs = 0;
    }

    info.rettype = PRIMTYPE_AUTO;

    if(hh->rettypes) // Return types specified?
    {
        HLFunctionHdr::Values rets = hh->nrets();
        info.nrets = rets.n;
        if(rets.variadic)
            info.flags |= FuncInfo::VarRets;

        info.rettype = typelistFromTypes(ft, hh->rettypes, nodeValueAsType);
    }

    // Search for return statements and see what they return
    {
        ReturnTypeDeductorVisitor vis = { ft, hh->rettypes, info.rettype };
        proto->body->visit(nodeReturnVisitor, NULL, &vis); // This will uncover type clashes if we already have a type
        Type rettype = vis.rettype;

        if(rettype == PRIMTYPE_AUTO && !vis.n) // No return type specified, no return found -> void aka nil
            rettype = PRIMTYPE_NIL;

        assert(rettype != PRIMTYPE_AUTO);

        info.rettype = rettype;

        if(rettype == PRIMTYPE_NIL)
            info.nrets = 0;
        else
        {
            TypeIdList tl = rt.tr.getlist(rettype);
            assert(tl.ptr);
            info.nrets = tl.n;
        }
    }

    info.flags |= FuncInfo::Proto;

    info.functype = rt.tr.mkfunc(info.paramtype, info.rettype);

    proto->info = info;

    funcroot->setknowntype(info.functype);

    return funcroot;
}

void HLNode::visit(HLPreVisitor pre, HLPostVisitor post, void* ud)
{
    HLPreVisitResult r = { VISIT_CONTINUE, 0 };
    if(pre)
        r = pre(this, ud);
    switch(r.res)
    {
        case VISIT_CONTINUE:
            if(const size_t N = numchildren())
            {
                HLNode **ch = children();
                for(size_t i = 0; i < N; ++i)
                    ch[i]->visit(pre, post, ud);
            }
        // fall through
        case VISIT_NOREC:
            if(post)
                post(this, ud, r.aux);
        // fall through
        case VISIT_ABORT:
            break;
    }
}

HLNode* HLNode::_clone(void* mem, size_t bytes, HLFoldTracker& ft) const
{
    byte * const begin = (byte*)mem;
    HLNode * const target = (HLNode*)begin;
    byte *end = this->_cloneRec(begin + sizeof(*this), target, ft);
    assert(end - begin == bytes);
    return target;
}

byte *HLNode::_cloneRec(byte *m, HLNode *target, HLFoldTracker& ft) const
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
                    d = &ch[i];
                    break;

                default:
                    d = &ch[i];
                    m = s->_cloneRec(m, d, ft); // Advances in our block
                    break;

                case HLNODE_FUNCTION:
                    assert(false); // should have been folded to funcproto at this point
                    //d = s->clone(ft); // Allocates its own memory
                    break;
            }
        }
        dst[i] = d;
    }

    return m;
}

void HLNode::_foldUnop(HLFoldTracker& ft)
{
    const Lexer::TokenType tt = Lexer::TokenType(tok);
    u.unary.opid = Lexer::TokenToUnOp(tt);
    assert(false);
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
void HLNode::propagateMyType(HLFoldTracker& ft, const HLNode *typesrc)
{
    assert(typesrc->isknowntype());

    if(this->isknowntype())
    {
        const char *toktxt = Lexer::GetTokenText(Lexer::TokenType(this->tok));
        ft.checktype(mytype, typesrc->mytype, toktxt, this, typesrc);
        return;
    }

    this->setknowntype(typesrc->mytype);

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
        u.aslist[i]->propagateMyType(ft, typesrc);
    }
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

void HLNode::_foldBinop(HLFoldTracker& ft)
{
    assert(type == HLNODE_BINARY);

    HLNode *L = u.binary.a;
    HLNode *R = u.binary.b;
    const Lexer::TokenType tt = Lexer::TokenType(tok);
    const char *opname = Lexer::GetTokenText(tt);
    Str name = ft.vm.rt->sp.put(opname);

    u.binary.opid = Lexer::TokenToBinOp(tt);

    GC &gc = ft.vm.rt->gc;

    Type ns = L->mytype;
    if(ns == PRIMTYPE_AUTO) // FIXME: not sure if this is ok
        ns = R->mytype;

    if(ns == PRIMTYPE_AUTO)
    {
        if(!ft.isAutoToAny())
            return; // Do not fold

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
        return;
    }
    const DFunc *fopr = opr->asFunc();
    if(!fopr)
    {
        std::ostringstream os;
        os << "type's '" << opname << "' is not a function";
        ft.error(this, os.str().c_str());
        return;
    }

    if(L->isconst() && R->isconst() && fopr->isPure())
    {
        Val stk[] = { L->u.constant.val, R->u.constant.val };
        int status = fopr->call(&ft.vm, stk); // FIXME: typecheck this
        assert(status == 1);
        // FIXME: handle error when call failed
        makeconst(gc, stk[0]);
        return;
    }

    HLNode *params = ft.hlir.list(gc, 2); // prealloc 2 elems
    params->u.list.add(L, gc);
    params->u.list.add(R, gc);

    unsafemorph<HLResolvedCall>();
    u.resolvedcall.paramlist = params;
    u.resolvedcall.func = fopr;
    setknowntype(fopr->info.rettype);
}

void HLFoldTracker::error(const HLNode* where, const char *msg)
{
    printf("(%s:%u): %s\n", filename.c_str(), where->line, msg);
}

void HLFoldTracker::warn(const HLNode* where, const char *msg)
{
    printf("(%s:%u): Warning: %s\n", filename.c_str(), where->line, msg);
}

bool HLFoldTracker::checktype(Type sub, Type reference, const char *what, const HLNode* where, const HLNode* decl)
{
    if(vm.rt->tr.isListCompatible(sub, reference))
        return true; // all good

    // oh no. it's error time
    std::ostringstream os;
    os << what << " has mismatched type";
    error(where, os.str().c_str());
    error(decl, "(type should be same as here)");
    return false;
}
