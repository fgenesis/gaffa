#pragma once

#include "array.h"

struct TKey;

/*
Table aka hashmap, dict, key-value-store.
Inspired by Lua tables, but without the hashmap/array dualism;
if you need a container with integer indices, use an Array.
--
One special property of this table is that values are stored in a regular array,
with the following guarantees:
- The array size is always the number of key+value pairs in the table
- nil keys are not allowed, values can be anything (nil is a regular value, unlike in Lua)
- There are no guarantees about ordering, except these:
    - There are no holes in the array. When a key is removed, the last value is popped and moved into the freed slot.
    - When inserting a new key, its value is appended to the end of the array.
    - Indexing or iterating over (key,value) pairs gives values in the same order as they are stored in the array.
--
Notes:
- If you don't need the key, index the array directly since it's a bit faster.
*/

struct KV
{
    Val k, v;
};

class Table
{
public:
    static Table *GCNew(GC& gc, Type kt, Type vt);
    void dealloc(GC& gc);
    void clear();
    Val get(Val k) const;
    Val set(GC& gc, Val k, Val v);
    Val pop(Val k);
    KV index(tsize idx) const;

    const DArray& values() { return vals; }
    tsize size() const { return vals.sz; }


protected:
    Table(tsize keytype, tsize valtype);
    tsize _addRef(GC& gc, uint v);
    uint _unRef(GC& gc, tsize ref);

private:

    TKey *_getkey(ValU findkey, tsize mask) const;
    void _cleanupforward(tsize idx);
    tsize _resize(GC& gc, tsize newsize);
    void _rehash(tsize oldsize, tsize newmask);

    DArray vals;
    TKey *keys;
    const Type keytype;
    tsize idxmask; // capacity = idxmask + 1
    tsize *backrefs;

    Table(const Table&); // forbidden
};

template<typename T>
class RefTable : private Table
{
public:
    RefTable() : Table(PRIMTYPE_UINT, PRIMTYPE_UINT) {}
    tsize addref(GC& gc, const T& x)
    {
        return arr.push_back(gc, x) ? _addRef(gc, arr.size()) : 0;
    }
    T *getref(tsize ref)
    {
        if(!ref)
            return NULL;
        uint idx = get(uint(ref - 1)).u.ui; // 0 if nil
        return idx ? &arr[idx] : NULL;
    }
    T *unref(GC& gc, tsize ref) // returns pointer that may be overwritten at the next non-const call
    {
        if(!ref)
            return NULL;
        uint idx = _unRef(ref).u.ui; // 0 if nil
        return idx ? &arr[idx] : NULL;
    }

private:
    PodArray<T> arr;
};
