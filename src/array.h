#pragma once

#include "typing.h"

class GC;

// Array with external allocator

#include "gc.h"

class Array
{
public:
    union
    {
        ValU *vals; // Any
        uint *ui;
        sint *si;
        unsigned char *b;
        real *f;
        Str *s;
        void *p;
        Type *ts;
        /*Range<uint> *ru;
        Range<sint> *ri;
        Range<real> *rf;*/
    } storage;
    size_t sz; // usable size in elements
    size_t cap; // capacity in elements
    const Type t;
    const unsigned elementSize;

    void *ensure(GC& gc, size_t n);
    void *_resize(GC& gc, size_t n);

    static Array *GCNew(GC& gc, size_t prealloc, Type t);

    Val dynamicLookup(size_t idx) const;
    void dealloc(GC& gc);
    inline void clear() { sz = 0; }

    Array(Type t);

    static void *_AllocStorage(GC& gc, void *p, size_t type, size_t oldelems, size_t newelems); // size in elements
};
