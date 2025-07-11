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

struct Type
{
    tsize id;
};

// The highest 2 bits of the type id are used as tags
enum TypeBits
{
    TB_OPTION  = size_t(1u) << size_t(sizeof(Type) * CHAR_BIT - 1u), // is option type
    TB_TYPEMASK = ~(TB_OPTION)
};

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
    PRIMTYPE_BOOL,   // ?  i         //
    PRIMTYPE_UINT,   // T  i         //
    PRIMTYPE_SINT,   // T  i         //
    PRIMTYPE_FLOAT,  // T  i         //
    PRIMTYPE_STRING, // T  R     g   //
    PRIMTYPE_TYPE,   // T  R     g   //
    PRIMTYPE_FUNC,   // T     S  G   //
    PRIMTYPE_VARARG, // T     S      // only for the type system, can't make an object out of this
    PRIMTYPE_TABLE,  // T  o  S  G   //
    PRIMTYPE_ARRAY,  // T  o  S  G   //
    PRIMTYPE_OBJECT, // T  o     G   //

    PRIMTYPE_ANY,    // can hold any value. must be last in the enum.
    // These are the engine-level types. Runtime-created types are any IDs after this.
    PRIMTYPE_MAX
};

struct _Nil {};

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

enum GCflags
{
    GCF_NOT_INTERESTING = 0,

    GCF_RECURSIVE = 0x0100, // object has children that must be traversed
    GCF_WEAK = 0x0200,
    CFG_MOD_MARK = 0x0400, // set whenever a container object is modified
};

// This is the base of every GC collectible object.
// Most likely (but not necessarily!) preceded in memory by a GCprefix, see gc.h
struct GCobj
{
    inline GCobj() : gcTypeAndFlags(0), gcsize(0) {}
    // Beware: These are overlaid with GCprefix members!

    // HMM: This could be merged. lower u8 is primtype, next 8 bits regular gc flags,
    // upper 16 bits reserved for internal gc things.
    // BUT: This needs to be pointer-size aligned, so we'd waste something on 64bit arch
    u32 gcTypeAndFlags;
    u32 gcsize;
};

template<typename T>
struct Range
{
    T begin, end, step;
};

// HMM: consider making ValU::u register sized.
// Move Range into a new IterSlot class that is bigger, for iteration.
// VM gets a stack of IterSlot; and each iteration can fill multiple slots, one per iterator
/*
- array (idx, array ptr) <- max idx via array ptr
- table (idx, table ptr) <- ditto
- ranges (begin, end, step)
- user (a, b, function)
*/

union _AnyValU
{
    u32 word;
    sint si;
    uint ui;
    real f;
    realui f_as_u; // same size as real
    void *p;
    sref str;
    GCobj *obj;
    /*Range<uint> urange;
    Range<sint> srange;
    Range<real> frange;*/
    Type t;
    uintptr_t opaque; // this must be large enough to contain all bits of the union
};

// dumb type, no ctors
struct ValU
{
    _AnyValU u;

    // This must not be PRIMTYPE_ANY.
    Type type; // Runtime type of this value (PrimType | TypeBits)

    void _init(tsize tyid);
    bool operator==(const ValU& o) const;
};

struct Val : public ValU
{
    inline Val(const _AnyValU u, Type t) { this->type = t; this->u = u; }
    inline Val(const ValU& v)           { this->u = v.u; this->type = v.type; }
    inline Val()                        { _init(PRIMTYPE_NIL); }
    inline Val(_Nil)                    { _init(PRIMTYPE_NIL); }
    inline Val(bool b)                  { _init(PRIMTYPE_BOOL);   u.ui = b; }
    inline Val(unsigned int i)          { _init(PRIMTYPE_UINT);   u.ui = i; }
    inline Val(int i)                   { _init(PRIMTYPE_SINT);   u.si = i; }
    inline Val(uint i, _Nil _ = _Nil()) { _init(PRIMTYPE_UINT);   u.ui = i; }
    inline Val(sint i, _Nil _ = _Nil()) { _init(PRIMTYPE_SINT);   u.si = i; }
    inline Val(real f)                  { _init(PRIMTYPE_FLOAT);  u.f = f; }
    inline Val(Str s)                   { _init(PRIMTYPE_STRING); u.str = s.id; }
    inline Val(_Str s)                  { _init(PRIMTYPE_STRING); u.str = s.ref; }
    inline Val(Type t)                  { _init(PRIMTYPE_TYPE);   u.t = t; }
    /*inline Val(const Range<uint>& r)    { _init(PRIMTYPE_URANGE); u.urange = r; }
    inline Val(const Range<sint>& r)    { _init(PRIMTYPE_SRANGE); u.srange = r; }
    inline Val(const Range<real>& r)    { _init(PRIMTYPE_FRANGE); u.frange = r; }*/
};

enum UnOpType
{
    UOP_INVALID, // 0
    UOP_NOT,
    UOP_POS,
    UOP_NEG,
    UOP_BIN_COMPL,
    UOP_TRY,
    UOP_UNWRAP,
    // 7
    // 8
    // 9
    // 10
    // 11
    // 12
    // 13
    // 14
    // 15
    UOP_MAX = 16
};

enum BinOpType
{
    OP_INVALID, // 0
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_INTDIV,
    OP_MOD,
    OP_BIN_AND,
    OP_BIN_OR,
    OP_BIN_XOR,
    OP_SHL,
    OP_SHR,
    OP_C_EQ,
    OP_C_NEQ,
    OP_C_LT,
    OP_C_GT,
    OP_C_LTE,
    OP_C_GTE,
    OP_C_AND,
    OP_C_OR,
    OP_EVAL_AND,
    OP_EVAL_OR,
    OP_CONCAT,
    OP_INDEX,
    // 24 wrap-add
    // 25 wrap-sub
    // 26 wrap-mul
    // 27 wrap-shl
    // 28
    // 29
    // 30
    // 31
    OP_MAX = 32
};

BinOpType BinOp_TokenToOp(unsigned tok);
UnOpType UnOp_TokenToOp(unsigned tok);

// Size of an element of type t, when multiple elements of this type are stored in an array
size_t GetPrimTypeStorageSize(unsigned t);


inline static bool isTrueish(const ValU v)
{
    static_assert(PRIMTYPE_BOOL == 2, "ey");
    return v.type.id > PRIMTYPE_BOOL && v.u.ui;
}

// Small-size memcpy() that's wired specifially to copy any values a ValU may contain.
// Assumes both pointers are aligned to 4 bytes.
void *valcpy(void *dst, const void *src, tsize bytes);
