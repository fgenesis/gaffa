#include "mlir.h"
#include "gainternal.h"

#include "mlir.h"
#include "hlir.h"
#include "strings.h"

#include <stdio.h>

static const char *symscopename(Symstore::Lookup info)
{
    switch(info.where)
    {
        case SCOPEREF_CONSTANT: return "constant";
        case SCOPEREF_LOCAL:    return "local";
        case SCOPEREF_UPVAL:    return "upvalue";
        case SCOPEREF_EXTERNAL: return "extern";
    }

    assert(false);
    return NULL;
}


static inline void invalidate(HLNode *n)
{
    n->type = HLNODE_NONE;
}


void MLIRContainer::_processRec(HLNode *n)
{
    if(!n || n->type == HLNODE_NONE)
        return;

    const ScopeType pushscope = _precheckScope(n);
    if(pushscope != SCOPE_NONE)
        syms.push(pushscope);

    const unsigned act = _pre(n);

    if(!(act & SKIP_CHILDREN))
    {
        unsigned N = n->_nch;
        HLNode **ch = &n->u.aslist[0];
        if(N == HLList::Children)
        {
            ch = n->u.list.list;
            N = n->u.list.used;
        }

        for(unsigned i = 0; i < N; ++i)
            _processRec(ch[i]);
    }

    _post(n);

    if(pushscope)
    {
        Symstore::Frame f;
        syms.pop(f);
        // TODO: close in inverse order
    }
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

void MLIRContainer::_codegen(HLNode* n)
{
    switch(n->type)
    {

    }

    unsigned N = n->_nch;
    HLNode **ch = &n->u.aslist[0];
    if(N == HLList::Children)
    {
        ch = n->u.list.list;
        N = n->u.list.used;
    }
    for(unsigned i = 0; i < N; ++i)
        _codegen(ch[i]);
}


unsigned MLIRContainer::_pre(HLNode* n)
{
    unsigned ret = 0;
    switch(n->type)
    {
        // -2, +2 is how to make explicit signed literals
        case HLNODE_UNARY:
            if(n->tok == Lexer::TOK_PLUS || n->tok == Lexer::TOK_MINUS)
            {
                HLNode *rhs = n->u.unary.rhs;
                if(rhs->type == HLNODE_CONSTANT_VALUE && rhs->u.constant.val.type.id == PRIMTYPE_UINT)
                {
                    *n = *rhs;
                    invalidate(rhs);
                    n->u.constant.val.type.id = PRIMTYPE_SINT;
                    return SKIP_CHILDREN;
                }
            }
            break;

        case HLNODE_IDENT:
            _refer(n, SYMREF_STANDARD);
            return SKIP_CHILDREN;

        case HLNODE_AUTODECL:
        {
            unsigned typeidx = _refer(n->u.autodecl.type, SYMREF_TYPE);
            _decl(n->u.autodecl.ident, SYMREF_STANDARD, typeidx);
            _processRec(n->u.autodecl.value);
            return SKIP_CHILDREN;
        }

        case HLNODE_VARDECLASSIGN: // var x, y, z = ...
        {
            // Process exprs on the right first, to make sure that it can't refer the symbols on the left
            _processRec(n->u.vardecllist.vallist);
            _processRec(n->u.vardecllist.decllist);
            return SKIP_CHILDREN;
        }

        case HLNODE_VARDEF:
        {
            unsigned typeidx = _refer(n->u.vardef.type, SYMREF_TYPE);
            unsigned flags = SYMREF_STANDARD;
            if(n->flags & DECLFLAG_MUTABLE)
                flags |= SYMREF_MUTABLE;
            _decl(n->u.vardef.ident, (MLSymbolRefContext)flags, typeidx);
        }
        break;

        case HLNODE_ASSIGNMENT:
        {
            const HLNode *dstlist = n->u.assignment.dstlist;
            const size_t N = dstlist->u.list.used;
            const HLNode * const * ch = dstlist->u.list.list;
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
                    else if(!(info.sym->usagemask & SYMREF_MUTABLE))
                    {
                        const char *name = curpool->lookup(c->u.ident.nameStrId);
                        const char *how = symscopename(info);
                        // TODO: annotation in code with line preview and squiggles -- how to get this info here?
                        printf("(%s:%u) error: assignment to fixed %s '%s' -- previously defined in line %u\n",
                            _fn, c->line, how, name, info.sym->linedefined);
                    }
                }
            }

        }

        case HLNODE_CALL:
            if(n->u.fncall.callee->type == HLNODE_IDENT)
                _refer(n->u.fncall.callee, SYMREF_CALL);
            break;

        case HLNODE_MTHCALL:
            if(n->u.mthcall.obj->type == HLNODE_IDENT)
                _refer(n->u.fncall.callee, SYMREF_HASMTH);
            break;

    }

    return ret;
}

void MLIRContainer::_post(HLNode* n)
{
    switch(n->type)
    {
        case HLNODE_BINARY:
        {
        }
        break;
        case HLNODE_IDENT:
        break;
    }
}

void MLIRContainer::_decl(HLNode* n, MLSymbolRefContext ref, unsigned typeidx)
{
    if(!n)
        return;
    assert(n->type == HLNODE_IDENT);
    const Symstore::Sym *clash = syms.decl(n->u.ident.nameStrId, n->line, ref);

    const char *name = curpool->lookup(n->u.ident.nameStrId);
    if(clash)
    {
        printf("(%s:%u) error: redefinition of '%s' -- first defined in line %u\n", _fn, n->line, name, clash->linedefined);
        // TODO: report error + abort
    }

    //printf("Declare %s in line %u\n", name.c_str(), n->line);

    MLDeclSym d;
    d.nameid = symbolnames.importFrom(*curpool, n->u.ident.nameStrId).id; // compress string IDs
    d.typeidx = typeidx;
    add(d);
}

unsigned MLIRContainer::_refer(HLNode* n, MLSymbolRefContext ref)
{
    if(!n)
        return 0;

    assert(n->type == HLNODE_IDENT);
    Symstore::Lookup info = syms.lookup(n->u.ident.nameStrId, n->line, ref);

    /*const char *how = symscopename(info);
    const std::string& name = curpool->lookup(info.sym->nameStrId);
    printf("Using %s in line %u as %s from line %u (symindex %d)\n",
        name.c_str(), n->line, how, info.sym->linedefined, info.symindex);*/

    return info.symindex;
}

MLIRContainer::MLIRContainer(GC& gc)
    : curpool(NULL), _fn(NULL), symbolnames(gc)
{
}

void MLIRContainer::_add(const unsigned* p, const size_t n, unsigned cmd)
{
    mops.push_back(cmd);
    for(size_t i = 0; i < n; ++i)
        mops.push_back(p[i]);
}

static std::string whatisit(unsigned ref)
{
    std::string s;
    if(ref & SYMREF_TYPE)
        s += "type, ";
    if(ref & SYMREF_CALL)
        s += "callable, ";
    if(ref &  SYMREF_INDEX)
        s += "indexable, ";
    if(ref & SYMREF_HASMTH)
        s += "has_methods, ";

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
    _processRec(root);
    Symstore::Frame top;
    syms.pop(top);

    if(size_t N = syms.missing.size())
    {
        printf("%s: There are %u external symbols referenced:\n", fn, unsigned(N));
        for(size_t i = 0; i < N; ++i)
        {
            const Symstore::Sym& s = syms.missing[i];
            std::string wh = whatisit(s.usagemask);
            const char *name = pool.lookup(s.nameStrId);
            printf("(%u)  %s (%s)\n", s.lineused, name, wh.c_str()); // TODO: first use in line xxx
        }
    }

    for(size_t i = 0; i < mops.size(); ++i)
    {
        printf("%08x\n", mops[i]);
    }

    this->curpool = NULL;
    return GAFFA_E_OK;
}
