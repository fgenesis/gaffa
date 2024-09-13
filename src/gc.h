#pragma once

#include "defs.h"

typedef void* (*Galloc)(void *ud, void *ptr, size_t osize, size_t nsize);

struct GCobj
{
    GCobj *gcnext;
    unsigned gccolor;
    unsigned _reserved_;
    size_t gcsize;
};

struct GC
{
    GCobj *curobj;
    unsigned curcolor;
    unsigned gcstep;
    Galloc alloc;
    void *gcud;
};

void gc_step(GC& gc, size_t n);
void *gc_new(GC& gc, size_t bytes);
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

template<typename T>
inline static T *gcnewobj(GC& gc)
{
    void *p = gc_new(gc, sizeof(T));
    return p ? GA_PLACEMENT_NEW(p) T();
}

