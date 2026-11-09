#include "mlir_debug.h"
#include "mlir.h"
#include "runtime.h"
#include "gaimpdbg.h"
#include "lex.h"
#include <stdio.h>
#include <vector>

static const char *opName(OperatorId op)
{
    if(const char *s = Lexer::GetOperatorName(op))
        return s;
    switch(op)
    {
        case OP_NEQ:    return "!=";
        case OP_LT:     return "<";
        case OP_LTE:    return "<=";
        case OP_GT:     return ">";
        case OP_GTE:    return ">=";
        case OP_LOGAND: return "&&";
        case OP_LOGOR:  return "||";
        case OP_QQM:    return "??";
        case OP_UNWRAP: return "unwrap";
        default: ;
    }
    assert(false);
    return "?";
}

static const char *cmdName(MLCmd cmd)
{
    switch(cmd)
    {
        case ML_LIST:      return "list";
        case ML_CONST:     return "const";
        case ML_VAR:       return "var";
        case ML_EXT:       return "ext";
        case ML_NAMEDECL:  return "namedecl";
        case ML_DECL:      return "decl";
        case ML_ASSIGN:    return "assign";
        case ML_IFELSE:    return "if";
        case ML_WHILE:     return "while";
        case ML_FOR:       return "for";
        case ML_GETINDEX:  return "getindex";
        case ML_GETNS:     return "getns";
        case ML_MTHCALL:   return "mthcall";
        case ML_FUNC:      return "func";
        case ML_RETURN:    return "return";
        case ML_EMIT:      return "emit";
        case ML_ITERPACK:  return "iterpack";
        case ML_NEW_ARRAY: return "newarray";
        case ML_NEW_TABLE: return "newtable";
        case ML_EXPORT:    return "export";
        case ML_CALLADJ:   return "calladj";
        case ML_SINK:      return "sink";
        case _ML_UPVAL:    return "upval";
        case _ML_LOCAL:    return "local";
        case _ML_FUNCIDX:  return "funcidx";
        case _ML_EMPTY:    return "empty";
        case _ML_DEAD:     return "DEAD";
        case _ML_VAL:      return "val";
        case _ML_UVAR:     return "uvar";
        case _ML_ERROR:    return "ERROR";
        default: ;
    }
    assert(false);
    return "???";
}

static void printVal(const Runtime& rt, const Val& v)
{
    switch(v.type)
    {
        case PRIMTYPE_NIL:    printf("nil"); break;
        case PRIMTYPE_BOOL:   printf("%s", v.u.ui ? "true" : "false"); break;
        case PRIMTYPE_UINT:   printf("%llu", (unsigned long long)v.u.ui); break;
        case PRIMTYPE_SINT:   printf("%lld", (long long)v.u.si); break;
        case PRIMTYPE_FLOAT:  printf("%g", (double)v.u.f); break;
        case PRIMTYPE_STRING: printf("\"%s\"", rt.sp.lookup(v.u.str).s); break;
        case PRIMTYPE_TYPE:   printf("type %s", getTypeName(rt, v.asDType()->tid).c_str()); break;
        case PRIMTYPE_FUNC:
        {
            const DebugInfo& d = v.asFunc()->dbg;
            printf("func");
            if(d.name)
                printf(" %s", rt.sp.lookup(d.name).s);
            if(d.linestart != u32(-1))
                printf(" (line %u)", (unsigned)d.linestart);
            break;
        }
        default:
        {
            const char *pn = GetPrimTypeName(v.type);
            printf("<%s %p>", pn ? pn : "?", v.u.p);
        }
    }
}

static void printVar(const MLIR& ml, const Runtime& rt, u32 idx, unsigned flags)
{
    const MLVar& v = ml.vars[idx];
    printf("'%s'", v.name ? rt.sp.lookup(v.name).s : "?");
    switch(v.kind)
    {
        case MLVar::MUTVAR: printf(" mut"); break;
        case MLVar::CONSTVAL: printf(" = "); printVal(rt, v.u.val); break;
        default: ;
    }
    if((flags & MLDUMP_TYPES) && v.kind != MLVar::CONSTVAL)
        printf(" : %s", getTypeName(rt, v.u.local.type).c_str());
}

// Named targets of a decl type list (or function params) are consecutive vars starting at firstvar
static void printDeclVars(const MLIR& ml, const Runtime& rt, const MLNode *typelist, u32 firstvar, unsigned flags)
{
    size_t n = 1;
    const MLNode *elems = typelist;
    if(typelist->m.cmd == ML_LIST)
    {
        n = typelist->list.len;
        elems = n ? typelist->firstChild() : NULL;
    }
    printf(" (");
    u32 varidx = firstvar;
    for(size_t i = 0; i < n; ++i)
    {
        if(i)
            printf(", ");
        if(elems[i].m.cmd == ML_SINK)
            printf(elems[i].m.p[0] ? "___" : "_");
        else
            printVar(ml, rt, varidx++, flags);
    }
    printf(")");
}

static void printNode(const MLIR& ml, const Runtime& rt, const MLNode *node, unsigned depth, unsigned flags)
{
    const MLCmd cmd = (MLCmd)node->m.cmd;

    printf("%5u ", (unsigned)ml.indexOf(node));
    const MLInfo *info = ml.infoOf(node);
    if(info && info->line)
        printf("L%-4u", info->line);
    else
        printf("     ");
    for(unsigned i = 0; i < depth; ++i)
        printf("  ");

    if(cmd == (MLCmd)OP_CALL)
        printf("call");
    else if(cmd >= _ML_OP_FIRST && cmd < _ML_OP_MAX)
        printf("op %s", opName((OperatorId)cmd));
    else
        printf("%s", cmdName(cmd));

    const u32 p0 = node->m.p[0];
    switch(cmd)
    {
        case ML_LIST:
            printf(" #%u", node->list.len);
            break;
        case _ML_VAL:
            printf(" ");
            printVal(rt, node->asVal());
            break;
        case ML_CONST:
        case _ML_UPVAL:
        case _ML_LOCAL:
            printf(" #%u", p0);
            break;
        case _ML_FUNCIDX:
        {
            printf(" #%u", p0);
            const sref name = p0 < ml.funcs.size() ? ml.funcs[p0].name : 0;
            if(name)
                printf(" '%s'", rt.sp.lookup(name).s);
            break;
        }
        case ML_VAR:
            printf(" ");
            printVar(ml, rt, p0, flags);
            break;
        case ML_EXT:
            printf(" '%s'", rt.sp.lookup(ml.externals[p0].name).s);
            break;
        case ML_NAMEDECL:
        {
            const MLNode *ns = node->firstChild();
            if(ns->m.cmd == ML_LIST && !ns->list.len)
                printf(" '%s' symid %u", p0 ? rt.sp.lookup(p0).s : "?", node->m.p[1]);
            else
            {
                printf(" namespaced ");
                printVar(ml, rt, node->m.p[1], flags);
            }
            break;
        }
        case ML_DECL:
            printDeclVars(ml, rt, node->firstChild(), p0, flags);
            if(node->firstChild()[1].m.cmd == ML_LIST && !node->firstChild()[1].list.len)
                printf(" deferred");
            break;
        case ML_FUNC:
            if(node->m.flags & MLFUNC_CONST)
                printf(" [const]");
            if(node->m.flags & MLFUNC_PURE)
                printf(" [pure]");
            printDeclVars(ml, rt, node->firstChild(), p0, flags);
            break;
        case ML_NEW_TABLE:
            printf(" numkv=%u", p0);
            break;
        case _ML_ERROR:
            printf(" \"%s\"", rt.sp.lookup(p0).s);
            break;
        default: ;
    }

    if((flags & MLDUMP_TYPES) && !mlirIsStmt(cmd) && cmd != _ML_ERROR && cmd != _ML_DEAD && cmd != _ML_UVAR)
        printf(" :: %s", getTypeName(rt, node->type()).c_str());

    printf("\n");
}

static void printTables(const MLIR& ml, const Runtime& rt, unsigned flags)
{
    for(size_t i = 0; i < ml.externals.size(); ++i)
    {
        const MLExternal& e = ml.externals[i];
        printf("EXT[%u] '%s'", (unsigned)i, rt.sp.lookup(e.name).s);
        if(e.val.type != PRIMTYPE_NOTYPE)
        {
            printf(" = ");
            printVal(rt, e.val);
        }
        printf("\n");
    }
    for(size_t i = 0; i < ml.vars.size(); ++i)
    {
        printf("VAR[%u] ", (unsigned)i);
        printVar(ml, rt, (u32)i, flags);
        printf("\n");
    }
    for(size_t i = 0; i < ml.funcs.size(); ++i)
    {
        const MLFunc& f = ml.funcs[i];
        printf("FUNC[%u] '%s' node %u, enclosing %d, %u locals, %u upvals\n", (unsigned)i,
            f.name ? rt.sp.lookup(f.name).s : "(anon)", (unsigned)f.myidx,
            (int)f.enclosingFuncIdxPlus1 - 1, (unsigned)f.locals.size(), (unsigned)f.upvals.size());
    }
}

void mlirDebugDump(const MLIR& ml, const Runtime& rt, unsigned flags)
{
    if(ml.nodes.size())
    {
        struct Item { u32 idx, depth; };
        std::vector<Item> stack; // not recursive: trees can be very deep
        Item root = { 0, 0 };
        stack.push_back(root);
        while(!stack.empty())
        {
            const Item it = stack.back();
            stack.pop_back();
            const MLNode *node = &ml.nodes[it.idx];
            printNode(ml, rt, node, it.depth, flags);

            const size_t nch = node->numchildren();
            const u32 first = nch ? (u32)ml.indexOf(node->firstChild()) : 0;
            for(size_t i = nch; i--; )
            {
                Item c = { u32(first + i), it.depth + 1 };
                stack.push_back(c);
            }
        }
    }

    if(flags & MLDUMP_TABLES)
        printTables(ml, rt, flags);
}
