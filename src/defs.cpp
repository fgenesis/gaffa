#include "defs.h"

#include "lex.h"
#include "util.h"

#include <string.h>

#include "symtable.h"
#include "gaobj.h"

      GCobj *Val::asAnyObj(PrimType prim)       { assert(prim == type); return u.obj; }
const GCobj *Val::asAnyObj(PrimType prim) const { assert(prim == type); return u.obj; }

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

static const unsigned char TypeElementSizes[] =
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
    /* PRIMTYPE_TABLE  */ sizeof(GCobj*), // table/array/object are always dynamically allocated
    /* PRIMTYPE_ARRAY  */ sizeof(GCobj*),
    /* PRIMTYPE_SYMTAB */ sizeof(SymTable*),
    /* PRIMTYPE_OBJECT */ sizeof(GCobj*),
    /* PRIMTYPE_ANY    */ sizeof(ValU), // both type + opaque
    /* PRIMTYPE_AUTO   */ sizeof(ValU),
    /* _PRIMTYPE_X_VARIADIC, */ 0,
    /* _PRIMTYPE_X_OPTIONAL, */ 0,
    /* _PRIMTYPE_X_SUBTYPE,  */ 0
};

size_t GetPrimTypeStorageSize(unsigned t)
{
    static_assert(sizeof(TypeElementSizes) == PRIMTYPE_MAX, "size mismatch");
    return t < Countof(TypeElementSizes) ? TypeElementSizes[t] : sizeof(ValU);
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
