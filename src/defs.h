#pragma once

#include "compiler-fixup.h"

#include <stddef.h>
#include <stdint.h>
#include <limits.h> // CHAR_BIT
#include <assert.h>

typedef size_t usize;
typedef int64_t sint;
typedef uint64_t uint;
typedef float real;
typedef uint32_t realui;

typedef uint32_t u32;
typedef uint16_t u16;
typedef unsigned char byte;

// "big enough" size type for containers. For when 64-bit size_t is too big and a smaller type is more practical.
typedef unsigned tsize;

// Type used for short hashes. There's no use to make this bigger than tsize
typedef unsigned uhash;

// Type used for string refs.
typedef unsigned sref;

// operator new() without #include <new>
// Unfortunately the standard mandates the use of size_t, so we need stddef.h the very least.
// Trick via https://github.com/ocornut/imgui
// "Defining a custom placement new() with a dummy parameter allows us to bypass including <new>
// which on some platforms complains when user has disabled exceptions."
struct GA_NewDummy {};
inline void* operator new(size_t, GA_NewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, GA_NewDummy, void*)       {}
#define GA_PLACEMENT_NEW(p) new(GA_NewDummy(), p)


enum Constants
{
    INT_BITS =  sizeof(uint) * CHAR_BIT,
};


typedef tsize Type;


// TODO: userdata; types are actually objects
enum PrimType
{
    // T/F/? - truthyness of value (always true, always false, depends)
    // R     - value is a ref ID
    // o     - value is based on GCobj
    // i     - value is stored completely inline in ValU
    // S     - type can be sub-typed
    // G     - value is garbage-collected directly
    // g     - value is garbage-collected indirectly only
    PRIMTYPE_NIL,    // F  i         // enum value must be 0
    PRIMTYPE_ERROR,  // F     S  g   //
    PRIMTYPE_OPAQUE, //    ?         //
    PRIMTYPE_BOOL,   // ?  i         //
    PRIMTYPE_UINT,   // T  i         //
    PRIMTYPE_SINT,   // T  i         //
    PRIMTYPE_FLOAT,  // T  i         //
    PRIMTYPE_STRING, // T  R     g   //
    PRIMTYPE_TYPE,   // T  R     g   //
    PRIMTYPE_FUNC,   // T  o  S  G   //
    PRIMTYPE_TABLE,  // T  o  S  G   //
    PRIMTYPE_ARRAY,  // T  o  S  G   //
    PRIMTYPE_SYMTAB, // T  o     G   //
    PRIMTYPE_OBJECT, // T  o     G   //

    PRIMTYPE_ANY,    // can hold any value. must be after specific types.
    PRIMTYPE_AUTO,   // special marker for type analysis
    // These are the engine-level types. Runtime-created types are any IDs after this.
    PRIMTYPE_MAX,

    _PRIMTYPE_FIRST_OBJ = PRIMTYPE_FUNC
};

struct _Nil {};
struct _Xnil {}; // the "invalid nil", used as sentinel, marker, etc

struct MemBlock
{
    const char *p;
    size_t n;
};

struct Str
{
    size_t len;
    sref id;
};

struct Strp
{
    const char *s; // 0-terminated
    size_t len;

    // convenience conversions
    inline operator const char *() const { return s; }
    inline operator MemBlock() const { MemBlock b = { s, len }; return b; }
};

struct _Str
{
    _Str(sref x) : ref(x) {}
    sref ref;
};

struct DType;
struct DFunc;
struct SymTable;
struct DObj;

enum
{
    GCOBJ_MASK_PRIMTYPE = 0xff
};

// This is the base of every GC collectible object.
// Most likely (but not necessarily!) preceded in memory by a GCprefix, see gc.h
struct GCobj
{
    // Beware: These are overlaid with GCprefix members and intentionally NOT initialized in a ctor!
    // The GC initializes the overlay part already and it MUST NOT be touched afterwards.

    // HMM: This could be merged. lower u8 is primtype, next 8 bits regular gc flags,
    // upper 16 bits reserved for internal gc things.
    // BUT: This needs to be pointer-size aligned, so we'd waste something on 64bit arch
    u32 gcTypeAndFlags;
    u32 gcsize;
    DType *dtype;
};

union _AnyValU
{
    u32 word;
    sint si;
    uint ui;
    real f;
    realui f_as_u; // same size as real
    void *p;
    sref str;
    GCobj *obj; // potentially DType, DObj, DFunc, ...
    uintptr_t opaque; // this must be large enough to contain all bits of the union
};

// dumb type, no ctors
struct ValU
{
    _AnyValU u;

    // This must not be PRIMTYPE_ANY.
    PrimType type; // Runtime type of this value

    void _init(PrimType tyid);
    bool operator==(const ValU& o) const;
};

struct Val : public ValU
{
    inline Val(const _AnyValU u, PrimType t) { this->type = t; this->u = u; }
    inline Val(const ValU& v)           { this->u = v.u; this->type = v.type; }
    inline Val()                        { _init(PRIMTYPE_NIL); }
    inline Val(_Nil)                    { _init(PRIMTYPE_NIL); }
    inline Val(_Xnil)                   { _init(PRIMTYPE_NIL);    u.opaque = 1; }
    explicit inline Val(bool b)                  { _init(PRIMTYPE_BOOL);   u.ui = b; }
    explicit inline Val(unsigned int i)          { _init(PRIMTYPE_UINT);   u.ui = i; }
    explicit inline Val(int i)                   { _init(PRIMTYPE_SINT);   u.si = i; }
    explicit inline Val(uint i, _Nil _ = _Nil()) { _init(PRIMTYPE_UINT);   u.ui = i; }
    explicit inline Val(sint i, _Nil _ = _Nil()) { _init(PRIMTYPE_SINT);   u.si = i; }
    explicit inline Val(real f)                  { _init(PRIMTYPE_FLOAT);  u.f = f; }
    explicit inline Val(Str s)                   { _init(PRIMTYPE_STRING); u.str = s.id; }
    explicit inline Val(_Str s)                  { _init(PRIMTYPE_STRING); u.str = s.ref; }
    explicit Val(DType *t);
    explicit Val(SymTable *symt);
    explicit Val(DFunc *func);
    explicit Val(DObj *o);


    Val(const void *func); // Not implemented, catch-all

    inline GCobj *asAnyObj(PrimType prim);
    DFunc    *asFunc();
    SymTable *asSymTab();
    DObj     *asDObj();
    DType    *asDType();

    inline const GCobj *asAnyObj(PrimType prim) const;
    const DFunc    *asFunc() const;
    const SymTable *asSymTab() const;
    const DObj     *asDObj() const;
    const DType    *asDType() const;
};

// Size of an element of type t, when multiple elements of this type are stored in an array
size_t GetPrimTypeStorageSize(unsigned t);


// Small-size memcpy() that's wired specifially to copy any values a ValU may contain.
// Assumes both pointers are aligned to 4 bytes.
void *valcpy(void *dst, const void *src, tsize bytes);
