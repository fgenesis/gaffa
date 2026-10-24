#pragma once

#include "defs.h"
#include "dedupset.h"

struct GC;
class Table;

class DType;

// Some rules:
// T... is variadic (0 or more)
// T? is an optional<T>
// (A, B) is a type list with 2 types
//

// Combine type id with these bits to modify the meaning
// ie. (PRIMTYPE_STRING | TYPEBIT_OPTIONAL | TYPEBIT_VARIADIC) is a string?...
// -> There is no separate optional<T> type, to not allow weird shit like
//    optional<optional<T>>.
enum TypeBits
{
    TYPEBIT_OPTIONAL = 1u << (sizeof(Type) * 8 - 1), // T -> T?
    TYPEBIT_VARIADIC = 1u << (sizeof(Type) * 8 - 2), // T -> T...

    TYPEBASE_MASK = ~(TYPEBIT_OPTIONAL | TYPEBIT_VARIADIC)
};

// TypeKind changes the semantic meaning of a type.
// plain typelist is a list of types without any special semantics
// struct is a type list whose entries are struct members and have names
// union is a typelist that behaves like 'any', but with constraints for the type system
// subtyped is a type list where the first entry is the base type, anything after it are subtypes
// primtype, struct, subtyped can create objects
// typelist & union can't
enum TypeKind
{
    TKIND_PRIMTYPE, // int, float, string, etc
    TKIND_TYPELIST, // (A, B, C)
    TKIND_UNION,    // type U = A | B | C
    TKIND_STRUCT,   // struct S { A a, B c, C c }; aka type S = { a=A, b=B, c=C }
    TKIND_SUBTYPED, // Table(string, any), encoded as (table, string, any)
                    // or Array(float), encoded as (array, float)
};

// everything that can create objects
static inline bool isObjectKind(TypeKind kind)
{
    return kind == TKIND_PRIMTYPE || kind == TKIND_STRUCT || kind == TKIND_SUBTYPED;
}

// everyting that can create objects, plus union type
static inline bool canDeclareVar(TypeKind kind)
{
    return kind != TKIND_TYPELIST;
}

enum { MAX_SUBTYPES = 2 };

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

    TypeKind kind;
    PrimType objprimtype; // Objects created of this type will have this as their underlying primtype
    tsize n; // n > 0, it's a struct
    tsize allocsize; // for the GC, but also adds some entropy for hashing/deduplication

    tsize defaultsOffset;
    tsize numdefaults;

    //Type members[n] follows behind the struct
    //sref nameStrIds[n] follows behind that. Only exists if it's a struct

    inline Type id() const { return h.tid; }

    inline const Type *types() const
    {
        return reinterpret_cast<const Type*>(this + 1);
    }
    inline const sref *names() const
    {
        return kind == TKIND_STRUCT
            ? reinterpret_cast<const sref*>(types() + size())
            : NULL;
    }

    inline Type *types()
    {
        return reinterpret_cast<Type*>(this + 1);
    }
    inline sref *names()
    {
        return kind == TKIND_STRUCT
            ? reinterpret_cast<sref*>(types() + size())
            : NULL;
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

TDesc *TDesc_New(const GC& gc, TypeKind kind, tsize n, tsize numdefaults, tsize extrasize);

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

struct FuncTypes
{
    TypeIdList params;
    TypeIdList rets;
};

class TypeRegistry
{
public:
    TypeRegistry(GC& gc);
    ~TypeRegistry();
    bool init();
    void dealloc();

    // Turn a list of type IDs into an ID, and register for later lookup. This does not create a DType.
    TDesc *mklist(const Type *ts, size_t n);

    TDesc *mkstruct(const StructMember *m, size_t n, size_t numdefaults);
    TDesc *mkstruct(const Table& t); // makes a struct from t (named)

    TDesc *mkprimDesc(PrimType t);
    DType *mkprim(PrimType t);

    // Helper to create a function type. Both params must be type lists (created via mklist())
    Type mkfunc(Type argt, Type rett);

    // Create a subtype of an existing internal type (eg. Table<string, Any>)
    Type mksub(PrimType prim, const Type *sub, size_t n);

    // TODO: function to make union

    const TDesc *lookupDesc(Type t) const; // get type descriptor
    DType *lookup(Type t);
    const TDesc *lookupFunc(FuncTypes *ft, Type t);

    // Compares whether sub fits inside a bigger type. Considers variadics and optionals.
    // Some examples:
    // (A, B) is compatible with (A, B...)
    // A is compatible with (A, B...) since B is variadic (ie. 0 or more)
    // (A, B, B) is compatible with (A, B...)
    // (A, nil) is compatible with (A, B?)
    // (A, B) is compatible with (A, B?) since a valid B is always a valid optional
    // (A, B...) is compatible with (A, B?) since no B makes B? nil
    // Negative examples:
    // A is not compatible with (A, B) since B is missing
    // (A, B?) is not compatible with (A, B) since B? doesn't guarantee a valid B

    bool isListCompatible(Type sub, Type bigger) const;

    FORCEINLINE void mark(Type t) { if(t > PRIMTYPE_MAX) _dedup.mark(t - PRIMTYPE_MAX); }

private:
    // Simple typelist creation without flattening
    TDesc *_mklist(TypeKind kind, const Type *ts, size_t n);
    bool _checkMustBeObjTypes(const Type *ts, size_t n);

    // Pointer to list start if plain typelist, or to single element
    TypeIdList _aslist(Type t);

    // How many type slots does type t require?
    // types with kind == TKIND_TYPELIST are recursively evaluated, everything else counts as 1
    size_t _listLenRec(const Type *ts, size_t n);
    TDesc *_listConcatFlatten(TypeKind kind, const Type *ts, size_t n);
    Type *_listCopy(Type *dst, const Type *ts, size_t n);
    // TODO: union unionizing

    Type _store(TDesc *);
    Dedup _dedup; // for structs and such (TDesc based)
    TDesc *_builtins[PRIMTYPE_MAX];
};
