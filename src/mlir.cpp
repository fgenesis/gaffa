#include "mlir.h"
#include "gainternal.h"

#include "mlir.h"
#include "hlir.h"
#include "strings.h"

#include <stdio.h>


static inline void invalidate(HLNode *n)
{
    n->type = HLNODE_NONE;
}


void MLIRContainer::_processRec(HLNode *n)
{
    if(!n || n->type == HLNODE_NONE)
        return;

    if(_pre(n))
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
}

void MLIRContainer::_checkblock(HLNode* n, bool push)
{
    switch(n->type)
    {

    }
}

bool MLIRContainer::_pre(HLNode* n)
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
                    return false;
                }
            }
            break;

        case HLNODE_IDENT:
            _refer(n, SYMREF_STANDARD);
            return false;

        case HLNODE_AUTODECL:
            _refer(n->u.autodecl.type, SYMREF_TYPE);
            _decl(n->u.autodecl.ident, SYMREF_STANDARD);
            _processRec(n->u.autodecl.value);
            return false;

        case HLNODE_VARDECLASSIGN: // var x, y, z = ...
        {
            // Process exprs on the right first, to make sure that it can't refer the symbols on the left
            _processRec(n->u.vardecllist.vallist);
            return false;
        }

        case HLNODE_CALL:
            if(n->u.fncall.callee->type == HLNODE_IDENT)
                _refer(n->u.fncall.callee, SYMREF_CALL);
            break;

        case HLNODE_MTHCALL:
            if(n->u.mthcall.obj->type == HLNODE_IDENT)
                _refer(n->u.fncall.callee, SYMREF_HASMTH);

    }


    return true;
}

void MLIRContainer::_post(HLNode* n)
{
    switch(n->type)
    {
        case HLNODE_VARDECLASSIGN: // var x, y, z = ...
        {
            // Process exprs on the right first, to make sure that it can't refer the symbols on the left
            const HLNode *decls = n->u.vardecllist.decllist;
            assert(decls->type == HLNODE_LIST);

            HLNode **ch = decls->u.list.list;
            const size_t N = decls->u.list.used;

            for(size_t i = 0; i < N; ++i)
            {
                HLNode *c = ch[i];
                assert(c->type == HLNODE_VARDEF);
                _refer(c->u.vardef.type, SYMREF_TYPE);
                _decl(c->u.vardef.ident, SYMREF_STANDARD);
            }


        }
        break;
    }
}

void MLIRContainer::_decl(HLNode* n, MLSymbolRefContext ref)
{
    if(!n)
        return;
    assert(n->type == HLNODE_IDENT);
    const Symstore::Sym *clash = syms.decl(n->u.ident.nameStrId, n->line, ref);

    const std::string& name = curpool->lookup(n->u.ident.nameStrId);
    if(clash)
        printf("Symbol clash: %s (already defined in line %u)\n", name.c_str(), clash->linedefined);
    else
        printf("Declare %s in line %u\n", name.c_str(), n->line);

}

void MLIRContainer::_refer(HLNode* n, MLSymbolRefContext ref)
{
    if(!n)
        return;
    assert(n->type == HLNODE_IDENT);
    Symstore::Lookup info = syms.lookup(n->u.ident.nameStrId, n->line, ref);

    const char *how = NULL;
    switch(info.where)
    {
        case SCOPEREF_CONSTANT: how = "constant"; break;
        case SCOPEREF_LOCAL: how = "local"; break;
        case SCOPEREF_UPVAL: how = "upval"; break;
        case SCOPEREF_EXTERNAL: how = "extern"; break;
    }

    const std::string& name = curpool->lookup(info.sym->nameStrId);

    printf("Using %s in line %u as %s from line %u\n", name.c_str(), n->line, how, info.sym->linedefined);
}

MLIRContainer::MLIRContainer()
    : curpool(NULL)
{
}

void MLIRContainer::_add(const unsigned* p, const size_t n)
{
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

GaffaError MLIRContainer::import(HLNode* root, const StringPool& pool)
{
    this->curpool = &pool;
    syms.push(SCOPE_FUNCTION);
    _processRec(root);
    Symstore::Frame top;
    syms.pop(top);

    if(size_t N = syms.missing.size())
    {
        printf("There are %u external symbols referenced:\n", unsigned(N));
        for(size_t i = 0; i < N; ++i)
        {
            const Symstore::Sym& s = syms.missing[i];
            std::string wh = whatisit(s.usagemask);
            const std::string& name = pool.lookup(s.nameStrId);
            printf("(%u)  %s (%s)\n", s.lineused, name.c_str(), wh.c_str()); // TODO: first use in line xxx
        }
    }
    this->curpool = NULL;
    return GAFFA_E_OK;
}
