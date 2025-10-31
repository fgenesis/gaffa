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
    assert(idxv.type == PRIMTYPE_UINT);
    return memberArray() + idxv.u.ui;
}

tsize DObj::memberOffset(const Val* pmember) const
{
    const Val *beg = memberArray();
    assert(beg <= pmember && pmember < beg + nmembers);
    return (const char*)pmember - (const char*)this;
}

// A type is an object of the type 'type'
DType::DType(TDesc* desc, DType* typeType)
    : tid(desc->h.tid), tdesc(desc)
    , fieldIndices(PRIMTYPE_STRING, PRIMTYPE_UINT)
{
    this->dtype = typeType;
    assert(typeType->tid == PRIMTYPE_TYPE);
}

DType* DType::GCNew(GC& gc, TDesc *desc, DType* typeType)
{
    const tsize nfields = typeType->numfields();
    const size_t sz = sizeof(DType);
    void *mem = gc_new(gc, sz, PRIMTYPE_TYPE);
    return mem ? GA_PLACEMENT_NEW(mem) DType(desc, typeType) : NULL;
}

DFunc* DFunc::GCNew(GC& gc)
{
    return (DFunc*)gc_new(gc, sizeof(DFunc), PRIMTYPE_FUNC);
}
