#pragma once

#include "array.h"


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

// There's no ValU in here due to possible alignment and padding issues with that type.
// Can't seem to convince the compiler to place validx in the otherwise unused padding section of ValU...
struct TKey
{
    _AnyValU u; // corresponds to ValU::u
    Type type;  // corresponds to ValU::type
    tsize validx; // index of value
};

class Table : public GCobj
{
public:
    static Table *GCNew(GC& gc, Type kt, Type vt);
    void dealloc(GC& gc);
    void clear();
    Val get(Val k) const;
    Val *getp(Val k);
    const Val *getp(Val k) const;
    Val set(GC& gc, Val k, Val v);
    Val pop(Val k);
    KV index(tsize idx) const;
    Val keyat(tsize idx) const;

    const DArray& values() const { return vals; }
    DArray& values() { return vals; }
    tsize size() const { return vals.sz; }

    void loadAll(const Table& o, GC& gc);

    Table(Type keytype, Type valtype);

    // Accessible because the GC needs this
    TKey *keys;

private:

    TKey *_getkey(ValU findkey, tsize mask) const;
    void _cleanupforward(tsize idx);
    tsize _resize(GC& gc, tsize newsize);
    void _rehash(tsize oldsize, tsize newmask);

    DArray vals;

public:
    const Type keytype;
private:
    tsize idxmask; // capacity = idxmask + 1
    tsize *backrefs;

    Table(const Table&); // forbidden
};
