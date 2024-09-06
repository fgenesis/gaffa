#include "array.h"
#include "gainternal.h"
#include "gc.h"
#include <string.h>


Array::Array(Type t)
    : t(t), sz(0), cap(0), elementSize(GetPrimTypeStorageSize(t.id))
{
    storage.p = NULL;
}

Array* Array::GCNew(GC& gc, tsize prealloc, Type t)
{
    void *storage = NULL;
    if(prealloc)
    {
        storage = AllocStorage(gc, NULL, t, 0, prealloc);
        if(!storage)
            return NULL;
    }

    void *pa = gc_new(gc, sizeof(Array));
    if(!pa)
    {
        if(storage)
            AllocStorage(gc, storage, t, prealloc, 0);
        return NULL;
    }

    Array *a = GA_PLACEMENT_NEW(pa) Array(t);
    a->storage.p = storage;
    a->cap = prealloc;
    return a;
}

Val Array::dynamicLookup(size_t idx) const
{
    if(idx >= sz)
        return _Nil();

    if(t.id >= PRIMTYPE_ANY)
        return storage.vals[idx];

    ValU v;
    v.type = t;

    const void *p = storage.b + (idx * elementSize);
    memcpy(&v.u, p, elementSize);
    return v;
}

void Array::dealloc(GC& gc)
{
    if(cap)
    {
        _resize(gc, 0);
        sz = 0;
    }
}

void* Array::ensure(GC& gc, tsize n)
{
    return n <= cap ? storage.p : _resize(gc, n);
}

void* Array::_resize(GC& gc, tsize n)
{
    void *p = AllocStorage(gc, storage.p, t, cap, n);
    if(p || !n)
    {
        storage.p = p;
        cap = n;
    }
}

void* Array::AllocStorage(GC& gc, void* p, Type t, tsize oldelems, tsize newelems)
{
    const size_t elemSize = GetPrimTypeStorageSize(t.id);
    const size_t oldbytes = oldelems * elemSize;
    const size_t newbytes = newelems * elemSize;
    return gc_alloc_unmanaged(gc, p, oldbytes, newbytes);
}

