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
typedef double widereal;
typedef uint32_t realui;

// fixed-size types
typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
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

#define MAGIC_FILE_ID_BYTES      0x1b, 'g', 'A', 0x1c
#define MAGIC_VERSION_BYTES      0, 0, 0
#define MAGIC_FILE_VARIANT(chr)  { MAGIC_FILE_ID_BYTES, chr, MAGIC_VERSION_BYTES }

/*enum MagicID
{
}*/


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
    // x     - type is fixed -- can't change state or be modified once created
    // 1     - type is a primitive value
    PRIMTYPE_NIL,    // F  i        1    // enum value must be 0
    PRIMTYPE_ERROR,  // F     S  g  x   //
    PRIMTYPE_OPAQUE, //    ?        1   //
    PRIMTYPE_BOOL,   // ?  i        1   //
    PRIMTYPE_UINT,   // T  i        1   //
    PRIMTYPE_SINT,   // T  i        1   //
    PRIMTYPE_FLOAT,  // T  i        1   //
    PRIMTYPE_STRING, // T  R     g  x   //
    PRIMTYPE_TYPE,   // T  o     g  x   //
    PRIMTYPE_FUNC,   // T  o  S  G  x   //
    PRIMTYPE_CORO,   // T  o  S  G      //
    PRIMTYPE_TABLE,  // T  o  S  G      //
    PRIMTYPE_ARRAY,  // T  o  S  G      //
    PRIMTYPE_SYMTAB, // T  o     G      //
    PRIMTYPE_OBJECT, // T  o     G      //

    PRIMTYPE_ANY,    // can hold any value. must be after specific types.

    PRIMTYPE_AUTO,     // special marker for type analysis
    _PRIMTYPE_X_SUBTYPE,  // special marker for subtype specialization
    PRIMTYPE_NORETURN, // special type that can't be constructed -> can't return this, ever
    // These are the engine-level types. Runtime-created types are any IDs after this.
    PRIMTYPE_NOTYPE,
    PRIMTYPE_MAX,

    _PRIMTYPE_FIRST_OBJ = PRIMTYPE_FUNC
};

inline static bool isObjectType(PrimType pt) { return pt >= PRIMTYPE_TYPE && pt < PRIMTYPE_ANY; }

// Type is a reference to an object that is mutable?
inline static bool isMutableRefType(PrimType pt) { return pt >= PRIMTYPE_CORO && pt < PRIMTYPE_ANY; }

struct _Nil {};
struct _Xnil {}; // the "invalid nil", used as sentinel, marker, etc
struct _Auto {}; // Invalid value marked as auto-type
struct _Notype {};

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
struct VM;

enum
{
    GCOBJ_MASK_PRIMTYPE = 0xff
};

// This is the base of every GC collectible object.
// Most likely (but not necessarily!) preceded in memory by a GCprefix, see gc.h
struct GCobj
{
    // -------------------------
    // Beware: These are overlaid with GCprefix members and intentionally NOT initialized in a ctor!
    // The GC initializes the overlay part already and it MUST NOT be touched afterwards.
    u32 gcTypeAndFlags;
    tsize gcsize;
    // -------------------------
    // Regular members below

    DType *dtype;


    inline PrimType primtype() const { return PrimType(gcTypeAndFlags & GCOBJ_MASK_PRIMTYPE); }
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

enum
{
    VALU_STORAGE_SIZE = sizeof(((ValU*)NULL)->u) +  sizeof(((ValU*)NULL)->type)
};

struct Val : public ValU
{
    inline Val(const _AnyValU u, PrimType t) { this->type = t; this->u = u; }
    inline Val(const ValU& v)           { this->u = v.u; this->type = v.type; }
    inline Val()                        { _init(PRIMTYPE_NIL); }
    inline Val(_Nil)                    { _init(PRIMTYPE_NIL); }
    inline Val(_Xnil)                   { _init(PRIMTYPE_NIL);    u.opaque = 1; }
    inline Val(_Auto)                   { _init(PRIMTYPE_AUTO); }
    inline Val(_Notype)                 { _init(PRIMTYPE_NOTYPE); }
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


    Val(const void *func) = delete; // Not implemented, catch-all

    inline GCobj *asAnyObj(PrimType prim) { return isObjectType(type) ? u.obj : NULL; }
    inline GCobj *asObj(PrimType prim) { return prim == type ? u.obj : NULL; }
    DFunc    *asFunc();
    SymTable *asSymTab();
    DObj     *asDObj();
    DType    *asDType();

    inline const GCobj *asAnyObj() const { return isObjectType(type) ? u.obj : NULL; }
    inline const GCobj *asObj(PrimType prim) const { return prim == type ? u.obj : NULL; }
    const DFunc    *asFunc() const;
    const SymTable *asSymTab() const;
    const DObj     *asDObj() const;
    const DType    *asDType() const;

    // To check optional validity. false if nil or error.
    inline bool isValidValue() const { return type > PRIMTYPE_ERROR; }
};

// Size of an element of type t, when multiple elements of this type are stored in an array
size_t GetPrimTypeStorageSize(unsigned t);

const char *GetPrimTypeName(unsigned t);


// Small-size memcpy() that's wired specifially to copy any values a ValU may contain.
// Assumes both pointers are aligned to 4 bytes.
void *valcpy(void *dst, const void *src, tsize bytes);

// Some token have multiple meanings (unary vs binary).
// This is the actual operation behind a token in its intended role.
enum OperatorId
{
    OP_ERROR,

    // unary
    OP_UPLUS,
    OP_UNEG,
    OP_UNOT,
    OP_UBITNOT,
    OP_ULEN,

    // binary
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_FDIV,
    OP_IDIV,
    OP_MOD,
    OP_SHL,
    OP_SHR,
    OP_BITAND,
    OP_BITOR,
    OP_XOR,

    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LTE,
    OP_GT,
    OP_GTE,
    OP_LOGAND,
    OP_LOGOR,

    OP_CONCAT,
    OP_GETINDEX,
    OP_SETINDEX,
    OP_GETINDEXOPT,

    // other
    OP_CALL,

    _OP_MAX_FUNC,

    // Any operator past the limit is not encoded as an operator function (__op_add, ...)
    // and has no such name.
    OP_QQM = _OP_MAX_FUNC,
    OP_UNWRAP,

    _OP_MAX
};

static bool IsOperatorPrefix(const char *s);
const char *GetOperatorName(OperatorId op);
OperatorId GetOperatorFromName(const char *name);
size_t GetOperatorArity(OperatorId op);

enum VisitResult
{
    VISIT_CONTINUE, // Continue visiting children
    VISIT_NOREC, // Break recursion, but call post
    VISIT_ABORT, // Don't even call post
};


// C leaf function; fastest to call but has some restrictions:
// - Non-variadic, max(#parameters, #retvals) must be <= MINSTACK
// - The VM will not try to allocate extra stack, MINSTACK has to suffice
// - Read args from inout[0..], write return values to inout[0..]
// - Must NOT call back into the VM (no call frame is pushed)
// - Can not reallocate the VM stack
// - Can't have upvalues
// - You need to know the number of parameters and return values,
//   and the function must be registered correctly so the VM
//   and type system know this too.
// - Stack space is limited; up to MINSTACK usable slots total
// - Return how many values the function should return, or any RTError to throw a runtime error
// - To cause a runtime error, return any of RTError < 0
typedef int (*LeafFunc)(VM *vm, Val inout[]);



// Full-fledged C function; slower to call
// - May or may not be variadic (params, return values, or both)
// - Calling back into the VM is allowed
// - Can grow the stack
// - Can have upvalues
// ---- C function call protocol: ----
// - Parameters are in inout[0..nargs)
// - Write return values to inout[0..N), then return N (there will be enough space pre-allocated)
// - If variadic returning N values: in the function, do this:
//     inout = vm->stack_ensure(inout, N);
//   Then proceed as above.
// - To cause a runtime error, return a RTError value < 0
typedef int (*CFunc)(VM *vm, size_t nargs, Val *inout, Val *upvals);

