#include "array.h"
#include "gainternal.h"
#include "gc.h"
#include <string.h>


void* PodArrayBase::_enlarge(GC& gc, tsize elementSize)
{
    tsize n = ((sz + (sz >> 1u)) | 63u) + 1;
    return _chsize(gc, n, elementSize);
}

void* PodArrayBase::_chsize(GC& gc, tsize n, tsize elementSize)
{
    const size_t oldbytes = size_t(cap) * size_t(elementSize);
    const size_t newbytes = size_t(n) * size_t(elementSize);

    void *p = gc_alloc_unmanaged(gc, ptr, oldbytes, newbytes);
    if(p || !n)
    {
        ptr = p;
        cap = n;
        // This does intentionally not touch sz
    }
    return p;
}

NOINLINE void* PodArrayBase::_resize(GC& gc, tsize n, tsize elementSize)
{
    void *p = _chsize(gc, n, elementSize);
    if(p || !n)
        sz = n;
    return p;
}

// ------------------------

DArray::DArray(Type t)
    : t(t), sz(0), cap(0), elementSize(GetPrimTypeStorageSize(t.id))
{
    storage.p = NULL;
}

DArray::~DArray()
{
    assert(!cap);
}

DArray* DArray::GCNew(GC& gc, tsize prealloc, Type t)
{
    void *storage = NULL;
    if(prealloc)
    {
        storage = AllocStorage(gc, NULL, t, 0, prealloc);
        if(!storage)
            return NULL;
    }

    void *pa = gc_new(gc, sizeof(DArray), PRIMTYPE_ARRAY);
    if(!pa)
    {
        if(storage)
            AllocStorage(gc, storage, t, prealloc, 0);
        return NULL;
    }

    DArray *a = GA_PLACEMENT_NEW(pa) DArray(t);
    a->storage.p = storage;
    a->cap = prealloc;
    return a;
}

void DArray::dealloc(GC& gc)
{
    if(cap)
    {
        _resize(gc, 0);
        sz = 0;
    }
}

void* DArray::ensure(GC& gc, tsize n)
{
    return n <= cap ? storage.p : _resize(gc, n);
}

void *DArray::_resize(GC& gc, tsize n)
{
    void *p = AllocStorage(gc, storage.p, t, cap, n);
    if(p || !n)
    {
        storage.p = p;
        cap = n;
    }
    return p;
}

void* DArray::AllocStorage(GC& gc, void* p, Type t, tsize oldelems, tsize newelems)
{
    const size_t elemSize = GetPrimTypeStorageSize(t.id);
    const size_t oldbytes = oldelems * elemSize;
    const size_t newbytes = newelems * elemSize;
    return gc_alloc_unmanaged(gc, p, oldbytes, newbytes);
}

Val DArray::dynamicLookup(tsize idx) const
{
    if(idx >= sz)
        return _Nil();

    if(t.id >= PRIMTYPE_ANY)
        return storage.vals[idx];

    ValU v;
    v.type = t;

    const void *p = storage.b + (idx * elementSize);
    valcpy(&v.u, p, elementSize);
    return v;
}

void DArray::dynamicAppend(GC& gc, ValU v)
{
    const tsize idx = sz;
    void *mem = ensure(gc, idx + 1);
    assert(mem); // TODO: handle OOM

    ++sz;

    if(t.id >= PRIMTYPE_ANY)
    {
        storage.vals[idx] = v;
        return;
    }

    void *p = storage.b + (size_t(idx) * elementSize);
    valcpy(p, &v.u, elementSize);
}

Val DArray::dynamicSet(tsize idx, ValU v)
{
    assert(idx < sz); // TODO: error
    ValU ret;
    if(t.id >= PRIMTYPE_ANY)
    {
        ret = storage.vals[idx];
        storage.vals[idx] = v;
    }
    else
    {
        ret.type = t;
        const tsize esz = elementSize;
        void *p = storage.b + (idx * esz);
        valcpy(&ret.u, p, esz);
        valcpy(p, &v.u, esz);
    }
    return ret;
}

Val DArray::removeAtAndMoveLast_Unsafe(tsize idx)
{
    assert(idx < sz);

    ValU v;
    const size_t lastidx = --sz;
    if(t.id >= PRIMTYPE_ANY)
    {
        v = storage.vals[idx];
        storage.vals[idx] = storage.vals[lastidx];
    }
    else
    {
        v.type = t;
        const void *last = storage.b + (lastidx * elementSize);
        void *p = storage.b + (size_t(idx) * elementSize);
        valcpy(&v.u, p, elementSize);
        valcpy(p, last, elementSize);
    }
    return v;
}

Val DArray::popValue()
{
    if(!sz)
        return _Nil();

    const tsize idx = sz - 1;
    Val v = dynamicLookup(idx);
    sz = idx;
    return v;
}
