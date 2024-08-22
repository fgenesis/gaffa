#include "array.h"
#include "gainternal.h"


Array::Array(Type t)
    : t(t)
{
}

Array* Array::New(const GaAlloc& ga, size_t n, size_t type)
{
    void *p = ga.alloc(ga.ud, NULL, 0, sizeof(Array));
    Type ty;
    ty.id = type;
    return p ? new (p) Array(ty) : NULL;
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

