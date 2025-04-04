#include "valstore.h"


ValStore::ValStore(GC& gc)
    : _gc(gc)
{
}

ValStore::~ValStore()
{
    vals.dealloc(_gc);
}

u32 ValStore::put(ValU v)
{
    const size_t N = vals.size();
    for(size_t i = 0; i < N; ++i) // FIXME: Make this smarter than a dumb linear search
    {
        if(vals[i] == v)
            return i;
    }

    ValU *p = vals.push_back(_gc, v);
    assert(p); // TODO: handle OOM
    return N;
}
