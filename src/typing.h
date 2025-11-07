#pragma once

#include "defs.h"
#include "dedupset.h"

struct GC;
class Table;

class DType;


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
    TDESC_BITS_IS_UNION   = 1 << 0, // is a union type, instead of a struct
    TDESC_BITS_NO_NAMES   = 1 << 1
};

struct TDescAlias
{
    Type t;
    uint counter;
};

struct _FieldDefault
{
    _AnyValU u; // This is NOT ValU and instead is split into two to make sure that there's
    PrimType t;     // no unused padding in TDesc that could interfere with deduplication.
    tsize idx;
};

// Type descriptor
// This struct is variable sized and must not contain pointers (except at the start of the struct)
// The struct memory itself is subject to deduplication (like strings), with the idea that
// multiple created types with the same members and properties are actually the same type.
struct TDesc
{
    struct // This is ignored for deduplication
    {
        DType *dtype;
        Type tid;
    } h;

    tsize n; // n > 0, it's a struct
    tsize allocsize; // for the GC, but also adds some entropy for hashing/deduplication

    // Defines what kind of type description this is.
    // - Always: primtype < PRIMTYPE_ANY
    // - if PRIMTYPE_ARRAY, n == 1 and the type that follows is the array specialization
    // - if PRIMTYPE_TABLE, n == 2 and the types that follow are key and value types
    // - if PRIMTYPE_OBJECT, n >= 0 and the types that follow are struct members
    // - if PRIMTYPE_FUNC, n == 2. first type is the func params as an ordered struct,
    //                               second is the same for the return values
    PrimType primtype;
    u32 bits;

    //Type members[n] follows behind the struct
    //sref nameStrIds[n] follows behind that. Doesn't exist if it's a union.

    tsize defaultsOffset;
    tsize numdefaults;


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

    inline _FieldDefault *defaults()
    {
        return reinterpret_cast<_FieldDefault*>(((char*)this) + defaultsOffset);
    }

    inline const _FieldDefault *defaults() const
    {
        return reinterpret_cast<const _FieldDefault*>(((const char*)this) + defaultsOffset);
    }

    FORCEINLINE tsize size() const { return n; }
};

TDesc *TDesc_New(const GC& gc, tsize n, u32 bits, tsize numdefaults, tsize extrasize);

struct StructMember
{
	Type t;
	sref name;
    Val defaultval; // xnil if no default
};

struct TypeIdList
{
    const Type *ptr;
    tsize n;
};

class TypeRegistry
{
public:
    TypeRegistry(GC& gc);
    ~TypeRegistry();
    bool init();
    void dealloc();

    // Turn a list of type IDs into an ID, and register for later lookup. This does not create a DType.
    Type mklist(const Type *ts, size_t n);

    // Get the type id of a previously stored list of type IDs. Returns 0 when not.
    Type lookuplist(const Type *ts, size_t n) const;

    // Use this if you need the type list back. if .ptr is NULL, the ID was not registered.
    TypeIdList getlist(Type t);



    Type mkstruct(const StructMember *m, size_t n, size_t numdefaults);
    Type mkstruct(const Table& t); // makes a struct from t (named)
    Type mkstruct(const DArray& t);

    TDesc *mkprimDesc(PrimType t);
    DType *mkprim(PrimType t);

    // Helper to create a function type
    Type mkfunc(Type argt, Type rett);

    // Create a subtype of an existing internal type (eg. Table<string, Any>)
    Type mksub(PrimType prim, const Type *sub, size_t n);

    // TODO: function to make union

    const TDesc *lookupDesc(Type t) const; // get type descriptor
    DType *lookup(Type t);

    FORCEINLINE void mark(Type t) { if(t > PRIMTYPE_MAX) _tt.mark(t - PRIMTYPE_MAX); }

private:
    Type _store(TDesc *);
    Dedup _tl; // for simple type lists (Type[] arrays only)
    Dedup _tt; // for structs and such (TDesc based)
    TDesc *_builtins[PRIMTYPE_MAX];
};
