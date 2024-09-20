#pragma once

#include "defs.h"
#include "dedupset.h"

struct GC;
class Table;

// Structs have no defined field order.
// But for the sake of making types comparable, the order of member types is sorted by ID.
/*
   +-TDesc (42)+          +-TDesc (23)---------+
   | tref = 23 | ------>  | tref, sub = 0      |
   +--(alias)--+          | x = PRIMTYPE_FLOAT |
                          | y = PRIMTYPE_FLOAT |
                          | z = PRIMTYPE_FLOAT |
                          +--------------------+

*/

enum TDescBits
{
    TDESC_BITS_IS_UNION   = size_t(1u) << size_t(sizeof(Type) * CHAR_BIT - 1u), // is a union type, instead of a struct
    TDESC_BITS_IS_ALIAS   = size_t(1u) << size_t(sizeof(Type) * CHAR_BIT - 2u), // only links to the actual type
    TDESC_LENMASK = ~(TDESC_BITS_IS_UNION | TDESC_BITS_IS_ALIAS)
};

struct TDescAlias
{
    Type t;
    uint counter;
};

// Type descriptor
struct TDesc
{
    tsize bits; // n = bits & TDESC_LENMASK. If n > 0, it's a struct
    tsize allocsize; // for the GC, but also adds some entropy for hashing/deduplication

    // Defines what kind of type description this is.
    // If it's an alias:
    // - Holds the original type. n == 0.
    // If it's not an alias:
    // - Always: t < PRIMTYPE_ANY
    // - if PRIMTYPE_ARRAY, n == 1 and the type that follows is the array specialization
    // - if PRIMTYPE_TABLE, n == 2 and the types that follow are key and value types
    // - if PRIMTYPE_OBJECT, n >= 0 and the types that follow are struct members
    // - if PRIMTYPE_STRUCT, n == 2. first type is the func params as an ordered struct,
    //                               second is the same for the return values
    Type t;

    //Type members[n] follows behind the struct
    //sref nameStrIds[] follows behind that. Doesn't exist if it's a union.


    inline const Type *types() const
    {
        return reinterpret_cast<const Type*>(this + 1);
    }
    inline const sref *names() const
    {
        return reinterpret_cast<const sref*>(types() + size());
    }

    inline Type *types()
    {
        return reinterpret_cast<Type*>(this + 1);
    }
    inline sref *names()
    {
        return reinterpret_cast<sref*>(types() + size());
    }

    FORCEINLINE tsize size() const { return bits & TDESC_LENMASK; }
};

TDesc *TDesc_New(const GC& gc, tsize numFieldsAndBits, tsize extrasize);

struct TypeAndName
{
	Type t;
	sref name;
};

class TypeRegistry
{
public:
    TypeRegistry(GC& gc);
    ~TypeRegistry();
    bool init();
    void dealloc();

    // Make a new type from t.
    // If normalize is true, reorder fields so that {x=int, y=int} is the same as {y=int, x=int}.
    // normalize=true is usually what you want for dynamically created types.
    // normalize=false
    Type construct(const Table& t, bool normalize); // makes a struct from t
    Type construct(const DArray& t);

    // Create an alias of an existing type that is not deduplicated and thus becomes an entirely different type
    Type mkalias(Type t);


    Type mksub(PrimType prim, const Type *sub, size_t n);

    // TODO: function to make union

    const TDesc *lookup(Type t) const; // get type descriptor directly (may be an alias)
    const TDesc *getstruct(Type t) const; // get underlying structure type (resolves aliases)

    FORCEINLINE void mark(Type t) { if(t.id > PRIMTYPE_MAX) _tt.mark(t.id - PRIMTYPE_MAX); }

private:
    Type _store(TDesc *);
    Type _mkalias(Type t, uint counter);
    bool _isAlias(Type t) const;
    Dedup _tt;
    Type _builtins[PRIMTYPE_MAX];
    uint _aliasCounter;
};
