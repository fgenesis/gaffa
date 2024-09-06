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
    tsize sz; // usable size in elements
    tsize cap; // capacity in elements
    const Type t;
    const tsize elementSize;

    void *ensure(GC& gc, tsize n);
    void *_resize(GC& gc, tsize n);

    static Array *GCNew(GC& gc, tsize prealloc, Type t);

    Val dynamicLookup(size_t idx) const;
    void dealloc(GC& gc);
    inline void clear() { sz = 0; }
    void dynamicAppend(GC& gc, ValU v);

    Array(Type t);

    static void *AllocStorage(GC& gc, void *p, Type type, tsize oldelems, tsize newelems); // size in elements
};
