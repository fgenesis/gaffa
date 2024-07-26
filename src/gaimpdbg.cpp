#include "gaimpdbg.h"
#include "hlir.h"


static const char *getLabel(HLNodeType t)
{
    switch(t)
    {
        case HLNODE_NONE:           return "none";
        case HLNODE_CONSTANT_VALUE: return "value";
        case HLNODE_UNARY:          return "unary";
        case HLNODE_BINARY:         return "binary";
        case HLNODE_TERNARY:        return "ternary";
        case HLNODE_CONDITIONAL:    return "if";
        case HLNODE_LIST:           return "(list)";
        case HLNODE_BLOCK:          return "(block)";
        case HLNODE_FORLOOP:        return "for";
        case HLNODE_WHILELOOP:      return "while";
        case HLNODE_ASSIGNMENT:     return "assign";
        case HLNODE_VARDECLASSIGN:  return "decl=";
        case HLNODE_VARDEF:         return "vardef";
        case HLNODE_AUTODECL:       return "autodecl";
        case HLNODE_DECLLIST:       return "decllist";
        case HLNODE_RETURN:         return "return";
        case HLNODE_CALL:           return "call";
        case HLNODE_MTHCALL:        return "methodcall";
        case HLNODE_IDENT:          return "ident";
        case HLNODE_NAME:           return "name";
        case HLNODE_TABLECONS:      return "table";
        case HLNODE_ARRAYCONS:      return "array";
        case HLNODE_RANGE:          return "range";
        case HLNODE_ITER_DECLLIST:  return "iterdecllist";
        case HLNODE_ITER_EXPRLIST:  return "iterexprlist";
        case HLNODE_INDEX:          return "index";
        case HLNODE_FUNCTION:       return "function";
        case HLNODE_FUNCTIONHDR:    return "functionhdr";
        case HLNODE_SINK:           return "sink";
    }
    return NULL;
}


#include <stdio.h>
#include "strings.h"

static void indent(size_t n)
{
    for(size_t i = 0; i < n; ++i)
        printf("  ");
}

static bool dump(const StringPool& p, const HLNode *n, unsigned level)
{
    const char *label = getLabel((HLNodeType)n->type);
    indent(level);
    printf("%s", label);

    if(n->_nch == HLList::Children)
    {
        printf(" # %u", (unsigned)n->u.list.used);
    }

    if(n->type == HLNODE_IDENT)
    {
        unsigned strid =  n->u.ident.nameStrId;
        if(strid)
        {
            const std::string& s = p.lookup(strid);
            printf(" \"%s\"", s.c_str());
        }
    }
    else if(n->type == HLNODE_VARDEF && n->u.vardef.ident->type == HLNODE_IDENT)
    {
        const HLVarDef& vd = n->u.vardef;
        printf("\n");
        indent(level+1);
        printf("Name: ");
        dump(p, vd.ident, 0);
        if(vd.type)
        {
            indent(level+1);
            printf("Type: ");
            dump(p, vd.type, 0);
        }
        else
        {
            indent(level+1);
            printf("(type is inferred)\n");
        }
        return true;
    }
    else if(n->type == HLNODE_VARDECLASSIGN)
    {
        if(n->flags & DECLFLAG_MUTABLE)
            printf(" [mutable!]");
    }
    else
    {
        const char *tt = n->tok ? Lexer::GetTokenText((Lexer::TokenType)n->tok) : "";
        if(tt && *tt)
            printf(" [%s]", tt);
    }

    printf("\n");
    return false;
}

static void dumprec(const StringPool& p, const HLNode *n, unsigned level)
{
    if(!n || n->type == HLNODE_NONE)
        return;

    if(dump(p, n, level))
        return;

    unsigned N = n->_nch;
    const HLNode * const *ch = &n->u.aslist[0];
    if(N == HLList::Children)
    {
        ch = n->u.list.list;
        N = n->u.list.used;
    }

    for(unsigned i = 0; i < N; ++i)
        dumprec(p, ch[i], level+1);
}

void hlirDebugDump(const StringPool& p, const HLNode *root)
{
    dumprec(p, root, 0);
}
