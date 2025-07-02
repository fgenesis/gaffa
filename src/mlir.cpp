#include "mlir.h"
#include "gainternal.h"

#include "mlir.h"
#include "hlir.h"
#include "strings.h"
#include "gaimpdbg.h"

#include <stdio.h>


static const char *symscopename(Symstore::Lookup info)
{
    switch(info.where)
    {
        case SCOPEREF_LOCAL:    return "local";
        case SCOPEREF_UPVAL:    return "upvalue";
        case SCOPEREF_EXTERNAL: return "extern";
    }

    assert(false);
    return NULL;
}

#if 0

static inline void invalidate(HLNode *n)
{
    n->type = HLNODE_NONE;
    const size_t N = n->_nch;
    n->_nch = 0;
    for(size_t i = 0; i < N; ++i)
    {
        HLNode *ch = n->u.aslist[i];
        invalidate(ch);
    }
}

static size_t nodesInChain(const MLNodeBase *n)
{
    size_t c = 0;
    do
    {
        ++c;
        n = n->hdr.next;
    }
    while(n);
    return c;
}

static size_t countChildren(const MLNodeBase *n)
{
    n = n->hdr.child;
    return n ? nodesInChain(n) : 0;
}

MLIRChainBuilder::MLIRChainBuilder(BlockListAllocator& bla, MLNodeBase *rootnode)
    : lastnode(NULL), rootnode(rootnode), bla(bla)
{
}

MLIRChainBuilder::~MLIRChainBuilder()
{
    assert(blockstack.empty());
    blockstack.dealloc(bla.gc);
}

MLNodeBase* MLIRChainBuilder::_allocnode(u32 sz, u32 n, u32 cmd, u32 nch)
{
    MLNodeBase *p = (MLNodeBase*)bla.alloc(sz);
    if(p)
    {
        p->hdr.numvals = 1 + n;
        p->hdr.xch = nch;
        //p->hdr.nch = 0; // Already 0. This is incremented as needed
        p->cmd = cmd;
    }
    return p;
}

// blockstack rules:
// - lastnode is always the last node that was added to the current chain
// - parent of lastnode is in blockstack.back()
// - predecessors of lastnode start at blockstack.back()->hdr.child. following ->next eventually leads to lastnode.

// new node is added to chain and becomes current node
void MLIRChainBuilder::append(MLNodeBase* n)
{
    MLNodeBase *parent = blockstack.empty() ? rootnode : blockstack.back();

    if(parent)
    {
        // One child is added to the parent node
        parent->hdr.nch++;
    }
    else
    {
        assert(!lastnode);
        blockstack.push_back(bla.gc, n);
    }

    if(lastnode) // NULL if the current chain is empty
    {
        assert(!lastnode->hdr.next);
        lastnode->hdr.next = n;
    }

    // if node is supposed to have children, start a new chain
    //if(n->hdr.xch)
    //    _push(n);

    lastnode = n;
}

MLNodeBase* MLIRChainBuilder::getRootNode()
{
    return rootnode;
}


MLNodeBase* MLIRChainBuilder::getFirstOverlayNode()
{
    return blockstack[0];
}

// current node becomes parent, new chain is started that become children of current node
void MLIRChainBuilder::_push(MLNodeBase * n)
{
    assert(!lastnode->hdr.child);
    lastnode->hdr.child = n; // Currently active node becomes new parent
    // Blockstack always contains the to-be-parent
    blockstack.push_back(bla.gc, lastnode); // TODO check alloc fail
    lastnode = NULL; // start new empty chain
}

/*
void MLIRChainBuilder::tryFinishChain()
{
    MLNodeBase *parent = blockstack.back();
    if(parent->hdr.nch >= parent->hdr.xch)
        pop();
}
*/

void MLIRChainBuilder::pop()
{
    _pop();
    // Ensure we got the exact number of children
    assert(lastnode->hdr.nch == lastnode->hdr.xch);
}

void MLIRChainBuilder::popBlock()
{
    _pop();
    assert(lastnode->cmd == ML_NOP);
}

void MLIRChainBuilder::_pop()
{
    lastnode = blockstack.pop_back();
    // Make sure we didn't miscount, but accept any number of children
    assert(countChildren(lastnode) == lastnode->hdr.nch);
}


// ---------------------------------------------------

MLIRContainer::ChainOverlay::ChainOverlay(MLIRContainer * self, MLNodeBase *rootnode)
    : chain(self->bla, rootnode), _self(self), _oldch(self->_chain)
{
    self->_chain = &chain;
}

MLIRContainer::ChainOverlay::~ChainOverlay()
{
    assert(_self->_chain == &chain);
    _self->_chain = _oldch;

    // Register newly built chain as child of root node
    if(MLNodeBase *root = chain.getRootNode())
    {
        assert(!root->hdr.child);
        root->hdr.child = chain.getFirstOverlayNode();
    }
}

MLIRChainBuilder * MLIRContainer::currentChain()
{
    return _chain;
}

// ---------------------------------------------------


void MLIRContainer::_handleChildrenOf(HLNode * n, MLNodeBase *parent)
{
    if(parent)
    {
        ChainOverlay ovr(this, parent);
        _handleChildren(n);
    }
    else
        _handleChildren(n);
}

#endif


void MLIRContainer::_handleChildren(HLNode * n)
{
    unsigned N = n->_nch;
    HLNode **ch = &n->u.aslist[0];
    if(N == HLList::Children)
    {
        ch = n->u.list.list;
        N = n->u.list.used;
    }

    for(unsigned i = 0; i < N; ++i)
        _handleNode(ch[i]);
}


ScopeType MLIRContainer::_precheckScope(HLNode* n)
{
    switch(n->type)
    {
        case HLNODE_FUNCTION:
            return SCOPE_FUNCTION;

        case HLNODE_CONDITIONAL:
        case HLNODE_WHILELOOP:
        case HLNODE_FORLOOP:
        case HLNODE_BLOCK:
            return SCOPE_BLOCK;
    }

    return SCOPE_NONE;
}

void MLIRContainer::_addLiteralConstant(ValU v)
{
    u32 *p = addn(2);
    p[0] = ML_LOADK;
    p[1] = literalConstants.put(v);
}

size_t MLIRContainer::add(u32 x)
{
    size_t idx = oplist.size();
    oplist.push_back(gc, x);
    return idx;
}

void MLIRContainer::add(const u32* p, size_t n)
{
    u32 *dst = addn(n);
    memcpy(dst, p, sizeof(*p) * n);
}

u32* MLIRContainer::addn(size_t n)
{
    return oplist.alloc_n(gc, n);
}

static MLSymbolRefContext buildDeclSymRefFlags(const HLNode *n)
{
    const DeclFlags d = (DeclFlags)n->flags;
    unsigned f = 0;
    if(d & DECLFLAG_EXPORT)
        f |= SYMREF_EXPORTED;
    if(d & DECLFLAG_MUTABLE)
        f |= SYMREF_MUTABLE;

    return (MLSymbolRefContext)f;
}


void MLIRContainer::_handleNode(HLNode* const n)
{
    if(!n || n->type == HLNODE_NONE)
        return;

    const ScopeType pushscope = _precheckScope(n);
    if(pushscope != SCOPE_NONE)
        syms.push(pushscope);

    bool skipChildren = false;

    switch(n->type)
    {
        case HLNODE_UNARY:
        {
            UnOpType op = UnOp_TokenToOp(n->tok);
            assert(op != UOP_INVALID);
            u32 *p = addn(2);
            p[0] = ML_UNOP;
            p[1] = op;
        }
        break;

        case HLNODE_BINARY:
        {
            BinOpType op = BinOp_TokenToOp(n->tok);
            assert(op != OP_INVALID);
            u32 *p = addn(2);
            p[0] = ML_BINOP;
            p[1] = op;
        }
        break;

        case HLNODE_IDENT:
            _getvalue(n, SYMREF_STANDARD);
            skipChildren = true;
            break;

        case HLNODE_FUNCDECL:
            _declInNamespace(n->u.funcdecl.ident, buildDeclSymRefFlags(n), n->u.funcdecl.namespac);
            _handleNode(n->u.funcdecl.value);
            skipChildren = true;
            break;

        case HLNODE_VARDECLASSIGN: // var x, y, z = ...
        {
            add(ML_DECL);
            // Process exprs on the right first, to make sure that it can't refer the symbols on the left
            _handleNode(n->u.vardecllist.decllist); // record decls, but ensure the vars are not visible yet
            _handleNode(n->u.vardecllist.vallist);

            // Remove SYMREF_NOTAVAIL flag from declared symbols -> they are now available
            assert(n->u.vardecllist.decllist->type == HLNODE_LIST);
            const HLNode * const *ch = n->u.vardecllist.decllist->children();
            const size_t N = n->u.vardecllist.decllist->numchildren();
            for(size_t i = 0; i < N; ++i)
            {
                const HLNode *def = ch[i];
                assert(def->type == HLNODE_VARDEF);
                const HLNode *ident = def->u.vardef.ident;
                assert(ident->type == HLNODE_IDENT);

                Symstore::Decl decl = syms.decl(ident->u.ident.nameStrId, ident->line, SYMREF_NOTAVAIL);
                assert(decl.clashing); // Must clash
                decl.clashing->referencedHow = (MLSymbolRefContext)(decl.clashing->referencedHow & ~SYMREF_NOTAVAIL);
            }

            skipChildren = true;
        }
        break;

        case HLNODE_VARDEF:
        {
            MLSymbolRefContext flags = buildDeclSymRefFlags(n);

            // Make sure these vars are not available yet, ie. 'int x = x' would declare the left x first
            // and then refer to it on the right side. This prevents that.
            // Vars are made visible later in HLNODE_VARDECLASSIGN.
            flags = (MLSymbolRefContext)(flags | SYMREF_NOTAVAIL);

            _declWithType(n->u.vardef.ident, flags, n->u.vardef.type, true);
        }
        break;

        case HLNODE_ASSIGNMENT:
        {
            HLNode *dstlist = n->u.assignment.dstlist;
            const size_t N = dstlist->numchildren();
            const HLNode * const * ch = dstlist->children();
            for(size_t i = 0; i < N; ++i)
            {
                const HLNode *c = ch[i];
                if(c->type == HLNODE_IDENT) // Assgnment directly to variable? Must be declared first
                {
                    Symstore::Lookup info = syms.lookup(c->u.ident.nameStrId, c->line, SYMREF_STANDARD);
                    if(info.where == SCOPEREF_EXTERNAL)
                    {
                        const char *name = curpool->lookup(c->u.ident.nameStrId);
                        // TODO: annotation in code with line preview and squiggles -- how to get this info here?
                        printf("(%s:%u) error: attempt to assign to undeclared identifier '%s'\n",
                            _fn, c->line, name);
                    }
                    else if(!(info.sym->referencedHow & SYMREF_MUTABLE))
                    {
                        const char *name = curpool->lookup(c->u.ident.nameStrId);
                        const char *how = symscopename(info);
                        // TODO: annotation in code with line preview and squiggles -- how to get this info here?
                        printf("(%s:%u) error: assignment to fixed %s '%s' -- previously defined in line %u\n",
                            _fn, c->line, how, name, info.sym->linedefined);
                    }
                }
            }
            // The checks above are just safeguards, preceed with children normally
        }
        break;

        case HLNODE_CALL:
            if(n->u.fncall.callee->type == HLNODE_IDENT)
                _refer(n->u.fncall.callee, SYMREF_CALL);
            add(ML_FNCALL);
            break;

        case HLNODE_MTHCALL:
            add(ML_MTHCALL);
            break;


        case HLNODE_FUNCTION:
        {
            assert(pushscope == SCOPE_FUNCTION);
            const HLFunctionHdr *hdr = n->u.func.hdr->as<HLFunctionHdr>();
            add(ML_FUNCDEF);
            {
                _handleChildren(hdr->paramlist);
                _handleChildren(hdr->rettypes);
                _handleChildren(n->u.func.body);
            }
            size_t locals = syms.peek().syms.size(); // FIXME: remove this
            skipChildren = true;
        }
        break;

        case HLNODE_FUNCTIONHDR: // handled as part of HLNODE_FUNCTION; shouldn't reach this
            assert(false);
            break;


        case HLNODE_CONSTANT_VALUE:
            _addLiteralConstant(n->u.constant.val);
            break;

        case HLNODE_NONE:
            break;

        case HLNODE_LIST:
        case HLNODE_BLOCK:
        {
            add(ML_GROUP_BEGIN);
            _handleChildren(n);
            add(ML_GROUP_END);
            skipChildren = true;
            break;
        }

        case HLNODE_RETURNYIELD:
        {
            switch(n->tok)
            {
                case Lexer::TOK_RETURN: add(ML_RETURN); break;
                case Lexer::TOK_YIELD:  add(ML_YIELD);  break;
                case Lexer::TOK_EMIT:   add(ML_EMIT);   break;
                default:
                    assert(false);
            }
        }
        break;

        default:
            assert(false);
    }

    if(!skipChildren)
    {
        _handleChildren(n);
    }


    if(pushscope)
    {
        Symstore::Frame f;
        syms.pop(f);

        printf("Close scope type %u, boundary type %u, total %u locals\n", pushscope, f.boundary, (unsigned)f.syms.size());

        if(size_t n = f.syms.size())
        {
            for(size_t i = n; i --> 0; )
            {
                const Symstore::Sym& sym = f.syms[i];
                const char *name = curpool->lookup(sym.nameStrId);
                bool mustclose = sym.mustclose();
                const char *note = "";
                if(mustclose)
                {
                    note = " (CLOSE MUTABLE UPVAL)";
                    add(ML_CLOSEUPVAL);
                    add(n - i);
                }
                else if(!sym.used())
                {
                    note = " (UNUSED)";
                }
                printf("  -> Close '%s'%s\n", name, note);
            }

            add(ML_FORGET);
            add((u32)n);
        }
    }

}

void MLIRContainer::_declWithType(HLNode* n, MLSymbolRefContext ref, HLNode *typeexpr, bool checkdefined)
{
    assert(n->type == HLNODE_IDENT);

    if(!typeexpr)
    {
        if(ref & SYMREF_MUTABLE)
            add(ML_VAR);
        else
            add(ML_CONST);
    }
    else
    {
        if(ref & SYMREF_MUTABLE)
            add(ML_TVAR);
        else
            add(ML_TCONST);

        // Common case: The type is directly an identifier. Mark the symbol as containing a type.
        if(typeexpr->type == HLNODE_IDENT)
            _refer(typeexpr, SYMREF_TYPE);

        _handleNode(typeexpr);
    }

    const char *name = curpool->lookup(n->u.ident.nameStrId);

    if(checkdefined)
    {
        Symstore::Decl decl = syms.decl(n->u.ident.nameStrId, n->line, ref);

        if(Symstore::Sym *clash = decl.clashing)
        {
            printf("(%s:%u) error: redefinition of '%s' -- first defined in line %u\n", _fn, n->line, name, clash->linedefined);
            // TODO: report error + abort
        }
    }

    printf("Declare %s in line %u\n", name, n->line);

    Str sname = symbolnames.importFrom(*curpool, n->u.ident.nameStrId); // compress string IDs
    add(sname.id);

    // Caller must write next what is actually being declared
}

void MLIRContainer::_declInNamespace(HLNode* n, MLSymbolRefContext ref, HLNode *namespac)
{
    assert(!(ref && SYMREF_MUTABLE));
    if(!namespac)
    {
        _declWithType(n, ref, NULL, false);
        return;
    }

    assert(n->type == HLNODE_IDENT);
    const char *name = curpool->lookup(n->u.ident.nameStrId);

    const char *ns;
    if(namespac->type == HLNODE_IDENT)
        ns = curpool->lookup(namespac->u.ident.nameStrId);
    else
        ns = "(expr; no name available)";
    printf("Declare %s :: %s in line %u:\n", ns, name, n->line);

    Str sname = symbolnames.importFrom(*curpool, n->u.ident.nameStrId); // compress string IDs

    add(ML_DECL_NS);
    add(sname.id);

    _handleNode(namespac);
    // Caller must write next what is actually being declared
}

void MLIRContainer::_getvalue(HLNode * n, MLSymbolRefContext ref)
{
    Symstore::Lookup info = _refer(n, ref);
    u32 *d = addn(2);

    switch(info.where)
    {
        case SCOPEREF_LOCAL:
            d[0] = ML_REFLOCAL;
            d[1] = info.sym->localslot; // FIXME: Don't do local slot alloc here
            break;
        case SCOPEREF_UPVAL:
            d[0] = ML_REFUPVAL;
            d[1] = info.symindex; // FIXME?
            assert(info.symindex >= 0);
            break;
        case SCOPEREF_EXTERNAL:
            d[0] = ML_REFSYM;
            d[1] = info.symindex;
            break;
    }
}

Symstore::Lookup MLIRContainer::_refer(HLNode* n, MLSymbolRefContext ref)
{
    assert(n->type == HLNODE_IDENT);
    Symstore::Lookup info = syms.lookup(n->u.ident.nameStrId, n->line, ref);

    const char *how = symscopename(info);
    Strp name = curpool->lookup(info.sym->nameStrId);
    printf("Referring [%s] in line %u as %s from line %u (symindex %d)\n",
        name.s, n->line, how, info.sym->linedefined, info.symindex);

    assert(info.symindex);

    return info;
}

MLIRContainer::MLIRContainer(GC& gc)
    : /*_chain(NULL), bla(gc),*/ gc(gc), curpool(NULL), symbolnames(gc), literalConstants(gc), _fn(NULL)
{
}

static std::string whatisit(unsigned ref)
{
    std::string s;
    if(ref & SYMREF_TYPE)
        s += "type, ";
    if(ref & SYMREF_CALL)
        s += "callable, ";

    if(s.length() >= 2)
        s.resize(s.length() - 2);
    else
        s = "variable";
    return s;
}

GaffaError MLIRContainer::import(HLNode* root, const StringPool& pool, const char *fn)
{
    if(!symbolnames.init())
        return GAFFA_E_OUT_OF_MEMORY;

    this->_fn = fn;
    this->curpool = &pool;
    syms.push(SCOPE_FUNCTION);

    _handleNode(root);

    Symstore::Frame top;
    syms.pop(top);

    if(size_t N = literalConstants.vals.size())
    {
        printf("%s: There are %u literal constants used:\n", fn, unsigned(N));
        for(size_t i = 0; i < N; ++i)
        {
            ValU v = literalConstants.vals[i];
            printf("(%u)  type(%u)  %016llX\n", (unsigned)i, v.type.id, v.u.ui); // FIXME: pretty-print this
        }
    }

    if(size_t N = syms.missing.size())
    {
        printf("%s: There are %u external symbols referenced:\n", fn, unsigned(N));
        for(size_t i = 0; i < N; ++i)
        {
            const Symstore::Sym& s = syms.missing[i];
            std::string wh = whatisit(s.referencedHow);
            const char *name = pool.lookup(s.nameStrId);
            printf("(%u)  %s (%s)\n", s.lineused, name, wh.c_str()); // TODO: first use in line xxx
        }
    }


    for(size_t i = 0; i < oplist.size(); ++i)
    {
        printf("%08x (%d)\n", oplist[i], oplist[i]);
    }

    oplist.dealloc(gc);
    this->curpool = NULL;
    return GAFFA_E_OK;
}
