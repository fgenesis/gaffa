#pragma once

#include "defs.h"
#include "util.h"

struct ga_RT;

typedef void* (*Galloc)(void *ud, void *ptr, size_t osize, size_t nsize);

enum GCflagsExt // bits 0..7 are reserved for the gc base type; bits 8..15 are externally visible flags
{
    _GCF_FINALIZER = (1 << 8), // has a finalizer
    _GCF_PINNED = (1 << 9)     // is pinned and must not be collected
};




struct GCiter
{
    size_t idx;
};

struct GC;
struct GCprefix;

// Prototol: each walk consumes steps.
// return >0 means this many steps are left (ie. this function finished its job); if 0, more steps are needed
typedef size_t (*GCwalk)(ga_RT& rt, GCobj *obj, size_t steps);

struct GC
{
    GCprefix *normallywhite; // Regular objects
    GCprefix *grey;          // Grey list, used during mark phase
    GCprefix *pinned;
    GCprefix *tosplice;
    GCprefix *dead;
    GCiter iter;
    GCwalk walkfunc;
    unsigned phase;
    Galloc alloc;
    void *gcud;
    struct
    {
        size_t used;
        size_t live_objs;
    } info;
};


void gc_step(GC& gc, size_t n);
GCobj *gc_new(GC& gc, size_t bytes, PrimType gctype); // new object is uninitialized; use GA_PLACEMENT_NEW() to init
void *gc_alloc_unmanaged(GC& gc, void *p, size_t oldsize, size_t newsize);

template<typename T>
inline static T *gc_alloc_unmanaged_T(GC& gc, T *p, size_t oldnum, size_t newnum)
{
    return (T*)gc_alloc_unmanaged(gc, p, sizeof(T) * oldnum, sizeof(T) * newnum);
}

void *gc_alloc_unmanaged_zero(GC& gc, size_t size);

template<typename T>
inline static T *gc_alloc_unmanaged_zero_T(GC& gc, size_t num)
{
    return (T*)gc_alloc_unmanaged_zero(gc, sizeof(T) * num);
}


/*/
template<typename T>
inline static T *gcnewobj(GC& gc)
{
    void *p = gc_new(gc, sizeof(T));
    return p ? GA_PLACEMENT_NEW(p) T();
}
*/
