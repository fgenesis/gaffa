#pragma once
 
#include "defs.h"
#include "gainternal.h"

class Table;
class Array;

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
    size_t bits; // n = bits & TDESC_LENMASK. If n > 0, it's a struct
    //Table *initializers; // member values set at object creation

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

    inline Type *types()
    {
        return reinterpret_cast<Type*>(this + 1);
    }
    inline unsigned *names()
    {
        return reinterpret_cast<unsigned*>(types() + (bits & TDESC_LENMASK));
    }
};

TDesc *TDesc_New(const GaAlloc& ga, size_t numFieldsAndBits);

struct TypeAndName
{
	Type t;
	unsigned name;
};

class TypeRegistry
{
    TypeRegistry(const GaAlloc& ga);
    ~TypeRegistry();

    TDesc *construct(const Table& t); // makes a struct from t
    TDesc *construct(const Array& t);

    // TODO: function to make union

    unsigned lookup(const TypeAndName *tn, size_t n); // 0 when not found, Type::id otherwise

private:
    TDesc *_store(TDesc *);
    const GaAlloc& _ga;
};
