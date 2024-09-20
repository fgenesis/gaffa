#pragma once

#include "defs.h"
#include "util.h"

struct ga_RT;

typedef void* (*Galloc)(void *ud, void *ptr, size_t osize, size_t nsize);

enum GCflagsExt
{
    _GCF_GC_ALLOCATED = (1 << 16), // internally set when object was actually allocated via GC
    _GCF_COLOR_1 = (1 << 17),
    _GCF_COLOR_2 = (1 << 18),
    _GCF_COLOR_MASK = _GCF_COLOR_1 | _GCF_COLOR_2
};

struct GCprefix;

// This is the hidden, GC-only part in front of a GCobj.
struct GCheader
{
    GCprefix *gcnext;
};

// This struct overlays GCheader with the beginning of a GCobj
struct GCprefix
{
    GCheader hdr;
    // --------
    // This is overlaid in memory. The following fields are the same as in GCobj.
    u32 gcTypeAndFlags;
    u32 gcsize;

    enum { HDR_SIZE = sizeof(GCheader) };

    inline GCobj *obj() { return reinterpret_cast<GCobj*>(((char*)this) + HDR_SIZE); }
};

static GCprefix *prefixof(GCobj *obj)
{
    assert(obj->gcTypeAndFlags & _GCF_GC_ALLOCATED);
    return reinterpret_cast<GCprefix*>(((char*)obj) - GCprefix::HDR_SIZE);
}


struct GCiter
{
    size_t idx;
};

struct GC;

// Prototol: each walk consumes steps.
// return >0 means this many steps are left (ie. this function finished its job); if 0, more steps are needed
typedef size_t (*GCwalk)(ga_RT& rt, GCobj *obj, size_t steps);

struct GC
{
    GCprefix *curobj;
    GCiter iter;
    GCwalk walkfunc;
    unsigned curcolor;
    unsigned gcstep;
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
