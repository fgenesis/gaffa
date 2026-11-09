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
    PrimType pt = o->primtype();
    assert(pt == o->dtype->tdesc->objprimtype);
    _init(pt);
    u.obj = o;
}


DFunc      *Val::asFunc()     { return static_cast<DFunc*   >(asObj(PRIMTYPE_FUNC)); }
SymTable   *Val::asSymTab()   { return static_cast<SymTable*>(asObj(PRIMTYPE_SYMTAB)); }
DObj       *Val::asDObj()     { return static_cast<DObj*    >(asObj(PRIMTYPE_OBJECT)); }
DType      *Val::asDType()    { return static_cast<DType*   >(asObj(PRIMTYPE_TYPE)); }

const DFunc      *Val::asFunc()    const { return static_cast<const DFunc*   >(asObj(PRIMTYPE_FUNC)); }
const SymTable   *Val::asSymTab()  const { return static_cast<const SymTable*>(asObj(PRIMTYPE_SYMTAB)); }
const DObj       *Val::asDObj()    const { return static_cast<const DObj*    >(asObj(PRIMTYPE_OBJECT)); }
const DType      *Val::asDType()   const { return static_cast<const DType*   >(asObj(PRIMTYPE_TYPE)); }


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
    /* PRIMTYPE_UPVAL  */ 0,
    /* PRIMTYPE_NORETURN */ 0,
    /* PRIMTYPE_NOTYPE */ 0
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
    /* PRIMTYPE_UPVAL  */ "upval",
    /* PRIMTYPE_NORETURN */ "noreturn",
    /* PRIMTYPE_NOTYPEN */ "notype"
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
    assert(tyid < PRIMTYPE_ANY || tyid == PRIMTYPE_NOTYPE);
    static_assert(sizeof(_AnyValU) == sizeof(((_AnyValU*)NULL)->opaque), "opaque member must fill the entire struct");
    u.opaque = 0;
    type = tyid;
}

bool ValU::operator==(const ValU& o) const
{
    return type == o.type && u.opaque == o.u.opaque;
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
        case OP_UNEGWRAP:
        case OP_QQM:
        case OP_UNWRAP:
            return 1;
        case OP_SETINDEX: // obj[key] = value
            return 3;
        default: ;
    }

    return 2;
}

