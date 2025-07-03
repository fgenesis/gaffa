#pragma once

#include "array.h"

class GC;
struct BufSink;
class StringPool;

// index->value store for constant values,
// eg. literals in code

class ValStore
{
public:
    ValStore(GC& gc);
    ~ValStore();
    u32 put(ValU v);

    void serialize(BufSink *sk, const StringPool& sp) const;

    PodArray<ValU> vals;

private:
    GC& _gc;
};
