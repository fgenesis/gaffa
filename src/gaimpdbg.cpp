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
        case HLNODE_FUNCDECL:       return "funcdecl";
        case HLNODE_DECLLIST:       return "decllist";
        case HLNODE_RETURNYIELD:    return "return/yield";
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
        case HLNODE_EXPORT:         return "export";
        case HLNODE_FUNC_PROTO:     return "funcproto";
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

static void dumprec(const StringPool& p, const HLNode *n, unsigned level);

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
        unsigned strid = n->u.ident.nameStrId;
        if(strid)
        {
            const char *s = p.lookup(strid);
            printf(" \"%s\"", s);
        }
    }
    else if(n->type == HLNODE_RETURNYIELD)
    {
        if(n->tok == Lexer::TOK_RETURN)
            printf("return");
        else if(n->tok == Lexer::TOK_YIELD)
            printf("yield");
        else if(n->tok == Lexer::TOK_EMIT)
            printf("emit");
        else
        {
            printf("yield/return/emit/???");
            assert(false);
        }
    }
    else if(n->type == HLNODE_FUNC_PROTO)
    {
        printf(" -> Packaged function:\n");
        dumprec(p, n->u.funcproto.proto->node, level+1);
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
    {}//return;

    unsigned N = n->numchildren();
    const HLNode * const *ch = n->children();

    for(unsigned i = 0; i < N; ++i)
        dumprec(p, ch[i], level+1);
}

void hlirDebugDump(const StringPool& p, const HLNode *root)
{
    dumprec(p, root, 0);
}

// -----------------------------

