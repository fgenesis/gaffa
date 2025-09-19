#include "gaobj.h"
#include "gc.h"

DObj::DObj(DType * dty)
    : /*dfields(NULL),*/ nmembers(dty->numfields())
{
    this->dtype = dty;
    const _FieldDefault *def = dty->tdesc->defaults();
    const tsize n = dty->tdesc->numdefaults;
    Val * const m = memberArray();
    for(tsize i = 0; i < n; ++i)
        m[def[i].idx] = Val(def[i].u, def[i].t);
}

DObj* DObj::GCNew(GC& gc, DType* dty)
{
    const tsize nfields = dty->numfields();
    const size_t sz = sizeof(DObj) + nfields * sizeof(Val);
    void *mem = gc_new(gc, sz, PRIMTYPE_OBJECT);
    return mem ? GA_PLACEMENT_NEW(mem) DObj(dty) : NULL;
}


Val* DObj::member(const Val& key)
{
    Val idxv = dtype->fieldIndices.get(key);
    assert(idxv.type.id == PRIMTYPE_UINT);
    return memberArray() + idxv.u.ui;
}

tsize DObj::memberOffset(const Val* pmember) const
{
    const Val *beg = memberArray();
    assert(beg <= pmember && pmember < beg + nmembers);
    return (const char*)pmember - (const char*)this;
}

static const Type StrType = { PRIMTYPE_STRING };
static const Type UintType = { PRIMTYPE_UINT };

DType::DType(Type tid, TDesc* desc, DType* typeType)
    : DObj(typeType)
    , fieldIndices(StrType, UintType)
    , tid(tid), tdesc(desc)
{
    assert(typeType->tid.id == PRIMTYPE_TYPE);
}

DFunc* DFunc::GCNew(GC& gc)
{
    return (DFunc*)gc_new(gc, sizeof(DFunc), PRIMTYPE_FUNC);
}
