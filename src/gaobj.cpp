#include "gaobj.h"
#include "gc.h"

DObj::DObj(DType * dty)
    : dfields(NULL), nmembers(dty->tdesc->size())
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
    assert(false);
    return NULL; // TODO
}
