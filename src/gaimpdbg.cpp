#include "gaimpdbg.h"
#include "hlir.h"
#include "runtime.h"


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
#include <sstream>


static void buildTypeName(std::ostringstream& os, const Runtime& rt, Type t)
{
    if(t < PRIMTYPE_MAX)
    {
        prim:
        os << GetPrimTypeName(t);
    }
    else if(const TDesc *d = rt.tr.lookupDesc(t))
    {
        if(d->h.tid < PRIMTYPE_MAX)
        {
            os << GetPrimTypeName(d->h.tid);
        }
        else
        {
            os << "TODO:TDesc";
        }
    }
    else
    {
        TypeIdList tl = rt.tr.getlist(t);
        if(tl.n > 2 && tl.ptr[0] == _PRIMTYPE_X_SUBTYPE)
        {
            buildTypeName(os, rt, tl.ptr[1]);

            os << "<";
            for(size_t i = 2; i < tl.n; ++i)
            {
                buildTypeName(os, rt, tl.ptr[i]);
                if(i < tl.n - 1)
                    os << ",";
            }
            os << ">";
        }
        else
        {
            os << "(";
            for(size_t i = 0; i < tl.n; ++i)
            {
                buildTypeName(os, rt, tl.ptr[i]);
                if(i < tl.n - 1)
                    os << ",";
            }
            os << ")";
        }
    }

    if(t & TYPEBIT_OPTIONAL)
        os << "?";
    if(t & TYPEBIT_VARIADIC)
        os << "...";
}

static std::string getTypeName(const Runtime& rt, Type t)
{
    std::ostringstream os;
    buildTypeName(os, rt, t);
    return os.str();
}


static void indent(size_t n)
{
    for(size_t i = 0; i < n; ++i)
        printf("  ");
}

static void dumprec(const Runtime& rt, const HLNode *n, unsigned level);

static bool dump(const Runtime& rt, const HLNode *n, unsigned level)
{
    const char *label = getLabel((HLNodeType)n->type);
    indent(level);
    printf("%s [%s]", label, getTypeName(rt, n->mytype).c_str());

    if(n->_nch == HLList::Children)
    {
        printf(" # %u", (unsigned)n->u.list.used);
    }

    if(n->type == HLNODE_IDENT)
    {
        unsigned strid = n->u.ident.nameStrId;
        if(strid)
        {
            const char *s = rt.sp.lookup(strid);
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
        const FuncProto *fp = n->u.funcproto.proto;
        printf(" -- Packaged function %s -> %s:\n", getTypeName(rt, fp->info.paramtype).c_str(), getTypeName(rt, fp->info.rettype).c_str());
        dumprec(rt, fp->body, level+1);
    }
    else
    {
        const char *tt = n->tok ? Lexer::GetTokenText((Lexer::TokenType)n->tok) : "";
        if(tt && *tt)
            printf(" [%s]", tt);

        if(n->type == HLNODE_CONSTANT_VALUE)
        {
            Val v = n->u.constant.val;
            printf(" :: value = ");
            switch(v.type)
            {
                case PRIMTYPE_UINT: printf("%zu", v.u.ui); break;
                case PRIMTYPE_SINT: printf("%zi", v.u.si); break;
                case PRIMTYPE_BOOL: printf("%s", v.u.ui ? "true" : "false"); break;
                case PRIMTYPE_FLOAT: printf("%f", v.u.f); break;
                case PRIMTYPE_STRING: printf("\"%s\"", rt.sp.lookup(v.u.str).s); break;
                //case PRIMTYPE_FUNCTION: { const DFunc *df = v.asFunc(); printf("function %s in line %d", df->dbg ? p.get(df->dbg->name).s : "?", df->dbg ? df->dbg->linestart : -1); break; }
                default: printf("%08p, type=%d", v.u.p, v.type); break;
            }
        }
    }

    printf("\n");
    return false;
}

static void dumprec(const Runtime& rt, const HLNode *n, unsigned level)
{
    if(!n || n->type == HLNODE_NONE)
        return;

    if(dump(rt, n, level))
    {}//return;

    unsigned N = n->numchildren();
    const HLNode * const *ch = n->children();

    for(unsigned i = 0; i < N; ++i)
        dumprec(rt, ch[i], level+1);
}

void hlirDebugDump(const Runtime& rt, const HLNode *root)
{
    dumprec(rt, root, 0);
}

// -----------------------------

