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
        case HLNODE_RETURNEMIT:     return "return/emit";
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
        case HLNODE_INDEXASSIGN:    return "indexassign";
        case HLNODE_NS_INDEX:       return "nsindex";
        case HLNODE_FUNCTION:       return "function";
        case HLNODE_FUNCTIONHDR:    return "functionhdr";
        case HLNODE_SINK:           return "sink";
        case HLNODE_EXPORT:         return "export";
        case HLNODE_NS_INDEXASSIGN: return "nsindexassign";
        case HLNODE_BREAK:          return "break";
        case HLNODE_CONTINUE:       return "continue";
        case HLNODE_UNWRAPBIND:     return "unwrapbind";
        case HLNODE_ANDOR:          return "and/or";
        case HLNODE_VALBLOCK:       return "valblock";
        case HLNODE_VARARGS:        return "...";
    }
    return NULL;
}


#include <stdio.h>
#include "strings.h"
#include <sstream>


static void buildTypeName(std::ostringstream& os, const Runtime& rt, Type t);

static void buildTypeNames(std::ostringstream& os, const Runtime& rt, const Type *ts, size_t n, const char *sep)
{
    for(size_t i = 0; i < n; ++i)
    {
        if(i)
            os << sep;
        buildTypeName(os, rt, ts[i]);
    }
}

// Elements of a type list, or t itself if it's not a list
static void buildListElems(std::ostringstream& os, const Runtime& rt, Type t)
{
    const TDesc *d = rt.tr.lookupDesc(t);
    if(d && d->kind == TKIND_TYPELIST)
        buildTypeNames(os, rt, d->types(), d->n, ", ");
    else
        buildTypeName(os, rt, t);
}

static void buildSubtyped(std::ostringstream& os, const Runtime& rt, const TDesc *d)
{
    const Type *ts = d->types();
    switch(ts[0])
    {
        case PRIMTYPE_FUNC: // (func, params, rets)
        {
            os << "func(";
            buildListElems(os, rt, ts[1]);
            os << ")";
            const TDesc *rd = rt.tr.lookupDesc(ts[2]);
            if(!(rd && rd->kind == TKIND_TYPELIST && !rd->n))
            {
                os << " -> ";
                buildListElems(os, rt, ts[2]);
            }
            return;
        }
        case PRIMTYPE_ARRAY: os << "Array"; break;
        case PRIMTYPE_TABLE: os << "Table"; break;
        default:
        {
            const char *pn = GetPrimTypeName(ts[0]);
            os << (pn ? pn : "?");
        }
    }
    os << "(";
    buildTypeNames(os, rt, ts + 1, d->n - 1, ", ");
    os << ")";
}

// Type names in script syntax: Const(Table(string, int))?, func(int, float) -> bool, {x=float, y=float}, (A, B), A | B
static void buildTypeName(std::ostringstream& os, const Runtime& rt, Type t)
{
    if(t & TYPEBIT_CONST)
        os << "Const(";

    const Type base = t & TYPEBASE_MASK;
    const TDesc *d = rt.tr.lookupDesc(base);
    if(!d)
    {
        const char *pn = base < PRIMTYPE_MAX ? GetPrimTypeName(base) : NULL; // eg. auto has no TDesc
        if(pn)
            os << pn;
        else
            os << "<unknown type " << base << ">";
    }
    else switch(d->kind)
    {
        case TKIND_PRIMTYPE:
        {
            const char *pn = GetPrimTypeName(d->h.tid);
            os << (pn ? pn : "?");
            break;
        }
        case TKIND_TYPELIST:
            os << "(";
            buildTypeNames(os, rt, d->types(), d->n, ", ");
            os << ")";
            break;
        case TKIND_UNION:
            buildTypeNames(os, rt, d->types(), d->n, " | ");
            break;
        case TKIND_STRUCT:
        {
            const sref *names = d->names();
            os << "{";
            for(tsize i = 0; i < d->n; ++i)
            {
                if(i)
                    os << ", ";
                os << rt.sp.lookup(names[i]).s << "=";
                buildTypeName(os, rt, d->types()[i]);
            }
            os << "}";
            break;
        }
        case TKIND_SUBTYPED:
            buildSubtyped(os, rt, d);
            break;
    }

    if(t & TYPEBIT_CONST)
        os << ")";
    if(t & TYPEBIT_OPTIONAL)
        os << "?";
    if(t & TYPEBIT_VARIADIC)
        os << "...";
}

std::string getTypeName(const Runtime& rt, Type t)
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
    printf("%p ", n);
    indent(level);
    if(!n)
    {
        printf("(NULL)\n");
        return false;
    }
    const char *label = getLabel((HLNodeType)n->type);
    printf("%s [%s]", label, getTypeName(rt, n->mytype).c_str());

    if(n->_nch == HLList::Children)
    {
        printf(" # %u", (unsigned)n->u.list.used);
    }

    if(n->type == HLNODE_IDENT)
    {
        sref strid = n->u.ident.nameStrId;
        if(strid)
        {
            const char *s = rt.sp.lookup(strid);
            printf(" \"%s\"", s);
        }
    }
    if(n->type == HLNODE_NAME)
    {
        sref strid = n->u.name.nameStrId;
        if(strid)
        {
            const char *s = rt.sp.lookup(strid);
            printf(" %s", s);
        }
    }
    else if(n->type == HLNODE_RETURNEMIT)
    {
        if(n->tok == Lexer::TOK_RETURN)
            printf("return");
        else if(n->tok == Lexer::TOK_EMIT)
            printf("emit");
        else
        {
            printf("return/emit/???");
            assert(false);
        }
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
                case PRIMTYPE_FUNC:
                {
                    const DFunc *df = v.asFunc();
                    const DebugInfo& d = df->dbg;
                    printf("function %s in line %d", d.name ? rt.sp.lookup(d.name).s : "?", d.linestart);
                }
                break;

                default: printf("%08p, type=%d", v.u.p, v.type); break;
            }
        }
    }

    printf("\n");
    return false;
}

static void dumprec(const Runtime& rt, const HLNode *n, unsigned level)
{
    if(dump(rt, n, level))
    {}//return;

    if(!n)
        return;

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

