#include "defs.h"

#include "lex.h"
#include "util.h"

#include <string.h>

#include "symtable.h"
#include "gaobj.h"
#include "gacoro.h"


Val::Val(DType *t)       { _init(PRIMTYPE_TYPE);   u.obj = t; }
Val::Val(SymTable *symt) { _init(PRIMTYPE_SYMTAB); u.obj = symt; }
Val::Val(DFunc *func)    { _init(PRIMTYPE_FUNC);   u.obj = func; }

Val::Val(DObj *o)
{
    PrimType pt = (PrimType)(o->gcTypeAndFlags & GCOBJ_MASK_PRIMTYPE);
    assert(pt == o->dtype->tdesc->primtype);
    _init(pt);
    u.obj = o;
}


DFunc      *Val::asFunc()     { return static_cast<DFunc*   >(asAnyObj(PRIMTYPE_FUNC)); }
SymTable   *Val::asSymTab()   { return static_cast<SymTable*>(asAnyObj(PRIMTYPE_SYMTAB)); }
DObj       *Val::asDObj()     { return static_cast<DObj*    >(asAnyObj(PRIMTYPE_OBJECT)); }
DType      *Val::asDType()    { return static_cast<DType*   >(asAnyObj(PRIMTYPE_TYPE)); }

const DFunc      *Val::asFunc()    const { return static_cast<const DFunc*   >(asAnyObj(PRIMTYPE_FUNC)); }
const SymTable   *Val::asSymTab()  const { return static_cast<const SymTable*>(asAnyObj(PRIMTYPE_SYMTAB)); }
const DObj       *Val::asDObj()    const { return static_cast<const DObj*    >(asAnyObj(PRIMTYPE_OBJECT)); }
const DType      *Val::asDType()   const { return static_cast<const DType*   >(asAnyObj(PRIMTYPE_TYPE)); }


struct UintPair
{
    unsigned from, to;
};


static unsigned lookup(const UintPair a[], size_t N, unsigned val, unsigned def)
{
    for(size_t i = 0; i < N; ++i)
        if(val == a[i].from)
            return a[i].to;
    return def;
}

static const byte TypeElementSizes[] =
{
    /* PRIMTYPE_NIL    */ sizeof(u32),
    /* PRIMTYPE_ERROR  */ sizeof(sref),
    /* PRIMTYPE_OPAQUE */ sizeof(_AnyValU),
    /* PRIMTYPE_BOOL   */ sizeof(u32), // FIXME: do we need an extra bool type for compact array storage?
    /* PRIMTYPE_UINT   */ sizeof(uint),
    /* PRIMTYPE_SINT   */ sizeof(sint),
    /* PRIMTYPE_FLOAT  */ sizeof(real),
    /* PRIMTYPE_STRING */ sizeof(sref),
    /* PRIMTYPE_TYPE   */ sizeof(Type),
    /* PRIMTYPE_FUNC   */ sizeof(DFunc*), // TODO
    /* PRIMTYPE_CORO   */ sizeof(DCoro*), // TODO
    /* PRIMTYPE_TABLE  */ sizeof(GCobj*), // table/array/object are always dynamically allocated
    /* PRIMTYPE_ARRAY  */ sizeof(GCobj*),
    /* PRIMTYPE_SYMTAB */ sizeof(SymTable*),
    /* PRIMTYPE_OBJECT */ sizeof(GCobj*),
    /* PRIMTYPE_ANY    */ sizeof(ValU), // both type + opaque
    /* PRIMTYPE_AUTO   */ sizeof(ValU),
    /* _PRIMTYPE_X_SUBTYPE,  */ 0,
    /* PRIMTYPE_NORETURN */ 0
};

size_t GetPrimTypeStorageSize(unsigned t)
{
    static_assert(Countof(TypeElementSizes) == PRIMTYPE_MAX, "size mismatch");
    return t < Countof(TypeElementSizes) ? TypeElementSizes[t] : sizeof(ValU);
}

static const char *TypeNames[] =
{
    /* PRIMTYPE_NIL    */ "nil",
    /* PRIMTYPE_ERROR  */ "error",
    /* PRIMTYPE_OPAQUE */ "opaque",
    /* PRIMTYPE_BOOL   */ "bool",
    /* PRIMTYPE_UINT   */ "uint",
    /* PRIMTYPE_SINT   */ "sint",
    /* PRIMTYPE_FLOAT  */ "float",
    /* PRIMTYPE_STRING */ "string",
    /* PRIMTYPE_TYPE   */ "type",
    /* PRIMTYPE_FUNC   */ "func",
    /* PRIMTYPE_CORO   */ "coro",
    /* PRIMTYPE_TABLE  */ "table",
    /* PRIMTYPE_ARRAY  */ "array",
    /* PRIMTYPE_SYMTAB */ "symtab",
    /* PRIMTYPE_OBJECT */ "object",
    /* PRIMTYPE_ANY    */ "any",
    /* PRIMTYPE_AUTO   */ "auto",
    /* _PRIMTYPE_X_SUBTYPE,  */ "_x_subtype",
    /* PRIMTYPE_NORETURN */ "noreturn"
};

const char *GetPrimTypeName(unsigned t)
{
    static_assert(Countof(TypeNames) == PRIMTYPE_MAX, "size mismatch");
    return t < Countof(TypeNames) ? TypeNames[t] : NULL;
}

void* valcpy(void* dst, const void* src, tsize bytes)
{
    assert((uintptr_t(src) & 3) == 0);
    assert((uintptr_t(dst) & 3) == 0);
    const u32 *r = (const u32*)src;
    u32 *w = (u32*)dst;
    switch(bytes)
    {
        default: assert(false);
        case 16: *w++ = *r++;
        case 12: *w++ = *r++;
        case 8: *w++ = *r++;
        case 4: *w++ = *r++;
    }
    return w;
}

void ValU::_init(PrimType tyid)
{
    assert(tyid < PRIMTYPE_ANY);
    static_assert(sizeof(_AnyValU) == sizeof(((_AnyValU*)NULL)->opaque), "opaque member must fill the entire struct");
    u.opaque = 0;
    type = tyid;
}

bool ValU::operator==(const ValU& o) const
{
    return type == o.type && u.opaque == o.u.opaque;
}

static const char * s_OpNames[] =
{
    /* OP_ERROR    */ NULL,
    /* OP_UPLUS    */ "__op_uplus",
    /* OP_UNEG     */ "__op_uneg",
    /* OP_UNOT     */ "__op_not",
    /* OP_UBITNOT  */ "__op_bitnot",
    /* OP_ULEN     */ "__op_len",
    /* OP_ADD      */ "__op_add",
    /* OP_SUB      */ "__op_sub",
    /* OP_MUL      */ "__op_mul",
    /* OP_FDIV     */ "__op_div",
    /* OP_IDIV     */ "__op_idiv",
    /* OP_MOD      */ "__op_mod",
    /* OP_SHL      */ "__op_shl",
    /* OP_SHR      */ "__op_shr",
    /* OP_BITAND   */ "__op_bitand",
    /* OP_BITOR    */ "__op_bitor",
    /* OP_XOR      */ "__op_bitxor",
    /* OP_EQ       */ "__op_eq",
    /* OP_NEQ      */ "__op_neq",
    /* OP_LT       */ "__op_lt",
    /* OP_LTE      */ "__op_lte",
    /* OP_GT       */ "__op_gt",
    /* OP_GTE      */ "__op_gte",
    /* OP_LOGAND   */ "__op_and",
    /* OP_LOGOR    */ "__op_or",
    /* OP_CONCAT   */ "__op_concat",
    /* OP_GETINDEX */ "__op_getindex",
    /* OP_SETINDEX */ "__op_setindex"
};

static bool IsOperatorPrefix(const char *s)
{
    return s[0] == '_'
        && s[1] == '_'
        && s[2] == 'o'
        && s[3] == 'p'
        && s[4] == '_';
}

const char *GetOperatorName(OperatorId op)
{
    static_assert(Countof(s_OpNames) == _OP_MAX, "mismatch");
    return op < Countof(s_OpNames) ? s_OpNames[op] : NULL;
}

OperatorId GetOperatorFromName(const char *name)
{
    if(IsOperatorPrefix(name))
    {
        name += 5;
        for(size_t i = 1; i < Countof(s_OpNames); ++i) // skip OP_ERROR
            if(!strcmp(name, s_OpNames[i] + 5))
                return (OperatorId)i;
    }

    return OP_ERROR;
}

size_t GetOperatorArity(OperatorId op)
{
    if(op == OP_ERROR || op >= _OP_MAX)
        return 0;

    switch(op)
    {
        case OP_UPLUS:
        case OP_UNEG:
        case OP_UNOT:
        case OP_UBITNOT:
        case OP_ULEN:
            return 1;
        default: ;
    }

    return 2;
}
