#pragma once

#include "util.h"

struct GC;

// For PodArray. Externalized for type erasure to avoid code bloat.
class PodArrayBase
{
public:
    FORCEINLINE tsize size() const { return sz; }
    FORCEINLINE void clear() { sz = 0; }
    void *ptr;
    tsize sz;
    tsize cap;
protected:
    FORCEINLINE PodArrayBase() : ptr(NULL), sz(0), cap(0) {}
    NOINLINE void *_enlarge(GC& gc, tsize elementSize);
    NOINLINE void *_chsize(GC& gc, tsize n, tsize elementSize);
    NOINLINE void *_resize(GC& gc, tsize n, tsize elementSize);
private:
    PodArrayBase(const PodArrayBase&); // forbidden
};


// Intentionally extremely simple resizable array. Intended for POD types only since no ctor/dtor is called.
// All the fast paths are inlined.
template<typename T>
class PodArray : public PodArrayBase
{
public:
    FORCEINLINE PodArray() : PodArrayBase() {}

    FORCEINLINE ~PodArray() { assert(cap == 0); } // if this fires, call dealloc() first
    FORCEINLINE void dealloc(GC& gc) { (void)resize(gc, 0); }

    FORCEINLINE       T& operator[](tsize i)       { assert(i < sz); return data()[i]; }
    FORCEINLINE const T& operator[](tsize i) const { assert(i < sz); return data()[i]; }

    FORCEINLINE const T *data() const { return (const T*)this->ptr; }
    FORCEINLINE       T *data()       { return (T*)this->ptr; }

    FORCEINLINE T *reserve(GC& gc, tsize n) { return n <= cap ? data() : (T*)_chsize(gc, n, sizeof(T)); }
    FORCEINLINE T *resize(GC& gc, tsize n) { return (T*)_resize(gc, n, sizeof(T)); }

    FORCEINLINE T pop_back() { assert(sz); return arr[--sz]; }

    // Returns pointer to n free slots
    FORCEINLINE T *alloc_n(GC& gc, tsize n)
    {
        T *a = data();
        const tsize N = sz;
        if(N == cap)
        {
            a = _enlarge(gc);
            if(!a)
                return NULL;
        }
        sz = N + n;
        return a + N;
    }

    FORCEINLINE T *push_back(GC& gc, T x)
    {
        T *a = alloc_n(gc, 1);
        if(a)
            *a = x;
        return a;
    }

private:
    FORCEINLINE T *_enlarge(GC& gc) { return (T*)PodArrayBase::_enlarge(gc, sizeof(T)); }
};


// Dynamically typed array with external allocator
class DArray : public GCobj
{
public:
    union
    {
        ValU *vals; // Any
        uint *ui;
        sint *si;
        unsigned char *b;
        real *f;
        sref *s;
        void *p;
        Type *ts;
        GCobj *objs;
        /*Range<uint> *ru;
        Range<sint> *ri;
        Range<real> *rf;*/
    } storage;
    tsize sz; // usable size in elements
    tsize cap; // capacity in elements
    const Type t; // element type
    //const Type lowt; // low-level type, ie. what is actually stored. must be <= PRIMTYPE_ANY.
    const tsize elementSize;

    void *ensure(GC& gc, tsize n);
    void *_resize(GC& gc, tsize n);

    static DArray *GCNew(GC& gc, tsize prealloc, Type t);

    FORCEINLINE usize size() const { return sz; }
    Val dynamicLookup(tsize idx) const;
    void dealloc(GC& gc);
    inline void clear() { sz = 0; }
    void dynamicAppend(GC& gc, ValU v);
    Val dynamicSet(tsize idx, ValU v);
    Val removeAtAndMoveLast_Unsafe(tsize idx);
    //void pop(tsize n);
    Val popValue();

    DArray(Type t);
    ~DArray();

    static void *AllocStorage(GC& gc, void *p, Type type, tsize oldelems, tsize newelems); // size in elements
};
