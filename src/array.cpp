#include "array.h"
#include "gainternal.h"


Array::Array(Type t)
    : t(t), n(0), ncap(0)
{
    storage.p = NULL;
}

Array* Array::New(const GaAlloc& ga, size_t n, size_t type)
{
    void *p = ga.alloc(ga.ud, NULL, 0, sizeof(Array));
    Type ty;
    ty.id = type;
    Array *a = p ? GA_PLACEMENT_NEW(p) Array(ty) : NULL;
    if(n)
    {
        if(!a->resize(ga, n))
        {
            a->destroy(ga);
            a = NULL;
        }
    }
    return a;
}

Val Array::dynamicLookup(size_t idx) const
{
    if(idx >= n)
        return _Nil();

    switch(t.id)
    {
        case PRIMTYPE_SINT:
            return storage.si[idx];
        case PRIMTYPE_UINT:
            return storage.ui[idx];
        case PRIMTYPE_BOOL:
            return storage.b[idx];
        case PRIMTYPE_FLOAT:
            return storage.f[idx];
        case PRIMTYPE_STRING:
            return storage.s[idx];
        case PRIMTYPE_TYPE:
            return storage.ts[idx];
        case PRIMTYPE_URANGE:
            return storage.ru[idx];
        default: ;
    }
    return storage.vals[idx];
}

void Array::destroy(const GaAlloc& ga)
{
    if(ncap)
    {
        size_t elemSize = GetPrimTypeStorageSize(t.id);
        ga.alloc(ga.ud, storage.p, ncap * elemSize, 0);
    }
    this->~Array();
    ga.alloc(ga.ud, this, sizeof(Array), 0);
}

void* Array::ensure(const GaAlloc& ga, size_t n)
{
    return n <= ncap ?  storage.p : resize(ga, n);
}

void* Array::resize(const GaAlloc& ga, size_t n)
{
    return _alloc(ga, GetPrimTypeStorageSize(t.id), n);
}

void * Array::_alloc(const GaAlloc& ga, size_t elemSize, size_t n)
{
    const size_t oldbytes = ncap * elemSize;
    const size_t newbytes = n * elemSize;
    void *p = ga.alloc(ga.ud, storage.p, oldbytes, newbytes);
    if(p || !n)
    {
        storage.p = p;
        ncap = n;
    }
    return p;
}

