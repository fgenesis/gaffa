#pragma once

#include "util.h"

struct GC;

// For PodArray. Externalized for type erasure to avoid code bloat.
class PodArrayBase
{
public:
    FORCEINLINE tsize size() const { return sz; }
    FORCEINLINE bool empty() const { return !sz; }
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

    FORCEINLINE const T *pend() const { return data() + sz; }
    FORCEINLINE       T *pend()       { return data() + sz; }

    FORCEINLINE T *reserve(GC& gc, tsize n) { return n <= cap ? data() : _chsize(gc, n); }
    FORCEINLINE T *resize(GC& gc, tsize n) { return (T*)_resize(gc, n, sizeof(T)); }

    FORCEINLINE T pop_back() { assert(sz); return data()[--sz]; }
    FORCEINLINE void pop_n(tsize n) { assert(n <= sz); sz -= n; }

    FORCEINLINE T& back() { assert(sz); return data()[sz-1]; }
    FORCEINLINE const T& back() const { assert(sz); return arr[sz-1]; }

    // Returns pointer to n free slots
    FORCEINLINE T *alloc_n_exact(GC& gc, tsize n)
    {
        T *a = data();
        tsize newsz = sz + n;
        if(newsz > cap)
        {
            a = this->_chsize(gc, newsz);
            if(!a)
                return NULL;
        }
        sz = newsz;
        return a + newsz - n;
    }

    FORCEINLINE T *alloc_n(GC& gc, tsize n)
    {
        T *a = data();
        const size_t oldsz = sz;
        const tsize newsz = oldsz + n;
        if(newsz > cap)
        {
            size_t newcap = cap * 2 + sz; // alloc some more to amortize re-allocations
            a = this->_chsize(gc, newcap);
            if(!a)
                return NULL;
        }
        sz = newsz;
        return a + oldsz;
    }

    FORCEINLINE T *push_back(GC& gc, T x)
    {
        T *a = data();
        const tsize N = sz;
        if(N == cap)
        {
            a = this->_enlarge(gc);
            if(!a)
                return NULL;
        }
        sz = N + 1;
        a[N] = x;
        return &a[N];
    }

private:
    FORCEINLINE T *_chsize(GC& gc, tsize n) { return (T*)PodArrayBase::_chsize(gc, n, sizeof(T)); }
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
    } storage;
    tsize sz; // used size in elements
    tsize cap; // capacity in elements
    const Type t; // element type
    const tsize elementSize;

    void *ensure(GC& gc, tsize n);
    void *enlarge(GC& gc, tsize minsize);
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


template<typename T>
struct Heap
{
    void push(GC& gc, T e)
    {
        size_t sz = a.size();
        a.push_back(gc, e);
        _percolateUp(sz);
    }

    T pop()
    {
        size_t sz = a.size();
        assert(sz);
        const T root = a[0];
        a[0] = a[--sz];
        a.pop_back();
        if(sz > 1)
            _percolateDown(0);
        return root;
    }

    inline size_t size() const { return a.size(); }
    inline void clear() const { a.clear(); }
    void dealloc(GC& gc) { a.dealloc(gc); }

    PodArray<T> a;

private:
    void _percolateUp(size_t i)
    {
        const T e = a[i];
        while(i)
        {
            const size_t p = (i - 1u) >> 1u;
            const T c = a[p];
            if(!(e < c))
                break;
            a[i] = c; // parent is smaller, move it down
            i = p; // continue with parent
        }
        a[i] = e;
    }

    void _percolateDown(size_t i)
    {
        const T e = a[i];
        const size_t sz = a.size();
        size_t child;
        goto start;
        do
        {
            {
                // pick smaller child
                T c = a[child];
                if(++child < sz) // do we have a right child?
                {
                    const T r = a[child];
                    if(r < c)
                    {
                        c = r;
                        goto cmp; // yep, keep that child
                    }
                }
                --child; // nope, child wasn't there or not smaller
cmp:
                if(!(c < e))
                    break;
                a[i] = c;
                i = child;
            }
start:
            child = (i << 1u) + 1u;
        }
        while(child < sz);
        a[i] = e;
        _percolateUp(i);
    }
};

template<typename T>
class Queue // FIFO, circular, POD only
{
public:
    Queue() : r(0), w(0), mask(0), elems(NULL)
    {
    }

    ~Queue()
    {
        assert(!elems);
    }

    inline bool empty() const
    {
        return w == r;
    }

    T *push(GC& gc, T x)
    {
        if(((w + 1) & mask) == r)
            if(!_realloc(gc, mask ? 2 * (mask + 1) : 16))
                return NULL;

        T *p = &elems[w];
        w = (w + 1) & mask;
        *p = x;
        return p;
    }

    T pop()
    {
        assert(!empty());
        T *p = &elems[r];
        r = (r + 1) & mask;
        return *p;

    }

    void dealloc(GC& gc)
    {
        if(elems)
            gc_alloc_unmanaged_T<T>(gc, elems, mask + 1, 0);
        elems = NULL;
        mask = 0;
        r = 0;
        w = 0;
    }

private:

    T *_realloc(GC& gc, size_t newcap)
    {
        T *e = gc_alloc_unmanaged_T<T>(gc, NULL, 0, newcap);
        if(!e)
            return NULL;

        size_t i = 0;
        for(; !empty(); ++i)
            e[i] = pop();

        dealloc(gc);

        r = 0;
        w = i;
        elems = e;
        mask = newcap - 1;
        return e;
    }

    size_t r; // the next pop will read the element from this index
    size_t w; // always the next index to be written when pushed to
    size_t mask; // always power of 2 minus 1; 0 if empty; mask+1 == capacity
    T *elems;
};
