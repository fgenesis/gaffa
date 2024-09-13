#pragma once

#include "defs.h"
#include "table.h"

struct GC;

// Structs have no defined field order.
// But for the sake of making types comparable, the order of member types is sorted by ID.


enum TDescBits
{
    TDESC_BITS_IS_UNION  = size_t(1u) << size_t(sizeof(Type) * CHAR_BIT - 1u), // is a union type, instead of a struct
    TDESC_LENMASK = ~(TDESC_BITS_IS_UNION)
};

// Type descriptor
struct TDesc
{
    tsize bits; // n = bits & TDESC_LENMASK. If n > 0, it's a struct

    //Type members[n] follows behind the struct
    //sref nameStrIds[] follows behind that. Doesn't exist if it's a union.

    inline const Type *types() const
    {
        return reinterpret_cast<const Type*>(this + 1);
    }
    inline const sref *names() const
    {
        return reinterpret_cast<const sref*>(types() + (bits & TDESC_LENMASK));
    }

    inline Type *types()
    {
        return reinterpret_cast<Type*>(this + 1);
    }
    inline sref *names()
    {
        return reinterpret_cast<sref*>(types() + (bits & TDESC_LENMASK));
    }
};

TDesc *TDesc_New(const GC& gc, size_t numFieldsAndBits);

struct TypeAndName
{
	Type t;
	sref name;
};

class TypeRegistry
{
    TypeRegistry(GC& gc);
    ~TypeRegistry();

    Type construct(const Table& t); // makes a struct from t
    Type construct(const DArray& t);

    // TODO: function to make union

    tsize lookup(const TypeAndName *tn, size_t n); // 0 when not found, Type::id otherwise

private:
    Type _store(TDesc *);
    RefTable<TDesc*> _tt;
    GC& _gc;
};
