#pragma once

#include "typing.h"

// Array with external allocator

class Array
{
public:
    union
    {
        ValU *vals;
        uint *ui;
        sint *si;
        bool *b;
        real *f;
        Str *s;
        void *p;
        Type *ts;
        Range<uint> *ru;
        Range<sint> *ri;
        Range<real> *rf;
    } storage;
    size_t n; // usable size in elements
    size_t ncap; // capacity in elements
    const Type t;

    void *ensure(const GaAlloc& ga, size_t n);
    void *resize(const GaAlloc& ga, size_t n);

    static Array *New(const GaAlloc& ga, size_t n, size_t type);

private:
    Array(Type t);
    void *_alloc(const GaAlloc& ga, size_t elemSize, size_t n);
    template<typename T> void *allocT(const GaAlloc& ga, size_t n) { return _alloc(ga, sizeof(T), n); }
};
