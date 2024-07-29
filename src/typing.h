#pragma once

#include "defs.h"
class Table;

// Structs have no defined field order.
// But for the sake of making types comparable, the order of member types is sorted by ID.


enum TypeBits
{
    TDESC_BITS_IS_UNION  = size_t(1u) << size_t(sizeof(Type) * CHAR_BIT - 1u), // is a union type, instead of a struct
    TDESC_LENMASK = ~(TB_OPTION | TB_ARRAY)
};

// Type descriptor
struct TDesc
{
    size_t bits; // n = bits & TDESC_LENMASK. If n > 0, it's a struct
    Table *fields; // methods, constants, dynamic bindings
    //Type members[n] follows behind the struct
    //unsigned nameStrIds[] follows behind that. Doesn't exist if it's a union.

    inline const Type *types() const
    {
        return reinterpret_cast<const Type*>(this + 1);
    }
    inline const unsigned *names() const
    {
        return reinterpret_cast<const unsigned*>(types() + (bits & TDESC_LENMASK));
    }
};

TDesc *TDesc_New(GaAlloc *ga, size_t sizeAndBits);