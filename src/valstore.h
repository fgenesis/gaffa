#pragma once

#include "array.h"

class GC;

// index->value store for constant values,
// eg. literals in code

class ValStore
{
public:
    ValStore(GC& gc);
    ~ValStore();
    u32 put(ValU v);


    PodArray<ValU> vals;

private:
    GC& _gc;
};
